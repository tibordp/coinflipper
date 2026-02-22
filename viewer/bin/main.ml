open Lwt.Syntax

let static_dir = ref "/viewer-static"
let grpc_port = 50051

let read_file path =
  Lwt.catch
    (fun () ->
      let* ic = Lwt_io.open_file ~mode:Lwt_io.Input path in
      let* contents = Lwt_io.read ic in
      let* () = Lwt_io.close ic in
      Lwt.return_some contents)
    (fun _exn -> Lwt.return_none)

let respond_string ?(status = `OK) ~content_type ~body () =
  let headers = Cohttp.Header.of_list [ ("content-type", content_type) ] in
  Cohttp_lwt_unix.Server.respond_string ~status ~headers ~body ()

let handle_status ~grpc_address =
  let* result =
    Viewer_lib.Grpc_client.get_status ~address:grpc_address ~port:grpc_port
  in
  match result with
  | Ok status ->
    let body = Viewer_lib.Format_status.format status in
    respond_string ~content_type:"text/plain" ~body ()
  | Error msg ->
    respond_string ~status:`Service_unavailable ~content_type:"text/plain"
      ~body:(Printf.sprintf "Error: %s\n" msg)
      ()

let handle_metrics ~grpc_address =
  let* result =
    Viewer_lib.Grpc_client.get_status ~address:grpc_address ~port:grpc_port
  in
  match result with
  | Ok status ->
    let body = Viewer_lib.Format_metrics.format status in
    respond_string ~content_type:"text/plain; version=0.0.4" ~body ()
  | Error msg ->
    respond_string ~status:`Service_unavailable ~content_type:"text/plain"
      ~body:(Printf.sprintf "Error: %s\n" msg)
      ()

let handle_static ~path ~content_type =
  let file_path = Filename.concat !static_dir path in
  let* contents = read_file file_path in
  match contents with
  | Some body -> respond_string ~content_type ~body ()
  | None ->
    respond_string ~status:`Not_found ~content_type:"text/plain"
      ~body:"Not found\n" ()

let server ~grpc_address ~port =
  let callback _conn req _body =
    let uri = Cohttp.Request.uri req in
    let path = Uri.path uri in
    match path with
    | "/" | "/index.html" ->
      handle_static ~path:"index.html" ~content_type:"text/html"
    | "/status.txt" -> handle_status ~grpc_address
    | "/metrics" -> handle_metrics ~grpc_address
    | "/github.png" ->
      handle_static ~path:"github.png" ~content_type:"image/png"
    | _ ->
      respond_string ~status:`Not_found ~content_type:"text/plain"
        ~body:"Not found\n" ()
  in
  let server_fn = Cohttp_lwt_unix.Server.make ~callback () in
  Printf.printf "Viewer listening on port %d, gRPC target: %s:%d\n%!" port
    grpc_address grpc_port;
  Cohttp_lwt_unix.Server.create
    ~mode:(`TCP (`Port port))
    server_fn

let () =
  let usage = "viewer [--port PORT] [--static-dir DIR] GRPC_ADDRESS" in
  let port = ref 8080 in
  let grpc_address = ref "" in
  let anon_args = ref [] in
  let spec =
    [
      ("--port", Arg.Set_int port, " HTTP listen port (default: 8080)");
      ( "--static-dir",
        Arg.Set_string static_dir,
        " Static files directory (default: /viewer-static)" );
    ]
  in
  Arg.parse spec (fun arg -> anon_args := arg :: !anon_args) usage;
  (match List.rev !anon_args with
  | [ addr ] -> grpc_address := addr
  | _ ->
    Printf.eprintf "Error: expected exactly one positional argument (gRPC server address)\n";
    Arg.usage spec usage;
    exit 1);
  Lwt_main.run (server ~grpc_address:!grpc_address ~port:!port)
