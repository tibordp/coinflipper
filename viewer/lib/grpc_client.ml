open Lwt.Syntax

module Pb = Coinflipper.Coinflipper

let get_status ~address ~port:grpc_port =
  Lwt.catch
    (fun () ->
      let port_str = string_of_int grpc_port in
      let* addresses =
        Lwt_unix.getaddrinfo address port_str
          [ Unix.(AI_SOCKTYPE SOCK_STREAM) ]
      in
      match addresses with
      | [] -> Lwt.return_error "Could not resolve gRPC server address"
      | addr_info :: _ ->
        let fd =
          Lwt_unix.socket
            (Unix.domain_of_sockaddr addr_info.Unix.ai_addr)
            Unix.SOCK_STREAM 0
        in
        Lwt.finalize
          (fun () ->
            let* () = Lwt_unix.connect fd addr_info.Unix.ai_addr in
            let error_handler _err = () in
            let* connection =
              H2_lwt_unix.Client.create_connection ~error_handler fd
            in
            let encode, decode =
              Ocaml_protoc_plugin.Service.make_client_functions
                Pb.CoinFlipper.getStatus
            in
            let enc = encode () in
            let handler =
              Grpc_lwt.Client.Rpc.unary
                ~f:(fun response_promise ->
                  let* response = response_promise in
                  match response with
                  | None ->
                    Lwt.return
                      (Error
                         (Grpc.Status.v ~message:"No response received"
                            Grpc.Status.Internal))
                  | Some response -> (
                    match
                      decode (Ocaml_protoc_plugin.Reader.create response)
                    with
                    | Ok v -> Lwt.return (Ok v)
                    | Error e ->
                      Lwt.return
                        (Error
                           (Grpc.Status.v
                              ~message:
                                (Printf.sprintf "Decode error: %s"
                                   (Ocaml_protoc_plugin.Result.show_error
                                      e))
                              Grpc.Status.Internal))))
                (Ocaml_protoc_plugin.Writer.contents enc)
            in
            let do_request ?flush_headers_immediately ?trailers_handler
                request ~response_handler =
              H2_lwt_unix.Client.request connection
                ?flush_headers_immediately ?trailers_handler request
                ~error_handler ~response_handler
            in
            let* result =
              Grpc_lwt.Client.call ~service:"coinflipper.CoinFlipper"
                ~rpc:"GetStatus" ~scheme:"http" ~handler ~do_request ()
            in
            let* () = H2_lwt_unix.Client.shutdown connection in
            match result with
            | Ok (Ok status, _grpc_status) -> Lwt.return_ok status
            | Ok (Error grpc_err, _) ->
              Lwt.return_error
                (Printf.sprintf "gRPC error: %s"
                   (match Grpc.Status.message grpc_err with
                   | Some msg -> msg
                   | None -> "unknown"))
            | Error _e -> Lwt.return_error "H2 connection error")
          (fun () ->
            (* H2 shutdown may already close the underlying fd *)
            Lwt.catch
              (fun () -> Lwt_unix.close fd)
              (fun _exn -> Lwt.return_unit)))
    (fun exn ->
      Lwt.return_error
        (Printf.sprintf "Connection error: %s" (Printexc.to_string exn)))
