module Pb = Coinflipper.Coinflipper

let commify n =
  let s = string_of_int n in
  let len = String.length s in
  let buf = Buffer.create (len + len / 3) in
  for i = 0 to len - 1 do
    if i > 0 && (len - i) mod 3 = 0 then Buffer.add_char buf ',';
    Buffer.add_char buf s.[i]
  done;
  Buffer.contents buf

let commify_f64 v = commify (int_of_float v)

let timeify seconds =
  let parts = ref [] in
  let s = ref seconds in
  let days = !s / (3600 * 24) in
  s := !s mod (3600 * 24);
  let hours = !s / 3600 in
  s := !s mod 3600;
  let minutes = !s / 60 in
  let secs = !s mod 60 in
  if days <> 0 then parts := Printf.sprintf "%d days" days :: !parts;
  if hours <> 0 then parts := Printf.sprintf "%d hours" hours :: !parts;
  if minutes <> 0 then parts := Printf.sprintf "%d minutes" minutes :: !parts;
  if secs <> 0 then parts := Printf.sprintf "%d seconds" secs :: !parts;
  String.concat " " (List.rev !parts)

let result_array_from_flips flips =
  let result = Array.make 128 0 in
  List.iter
    (fun (flip : Pb.Coinflip.t) ->
      if flip.position >= 0 && flip.position < 128 then
        result.(flip.position) <- flip.flips)
    flips;
  result

let format (status : Pb.Coinstatus.t) =
  let buf = Buffer.create 4096 in
  let results = result_array_from_flips status.flips in
  let total_flips = commify status.total_flips in
  let fps = commify_f64 status.flips_per_second in
  let width = max (String.length total_flips) (String.length fps) in
  Buffer.add_string buf
    (Printf.sprintf "Total coins flipped: %*s\n" width total_flips);
  Buffer.add_string buf
    (Printf.sprintf "Coins per second:    %*s\n" width fps);
  Buffer.add_char buf '\n';
  (* Connected clients sorted by speed (descending) *)
  let stats =
    List.sort
      (fun (a : Pb.Coinstats.t) (b : Pb.Coinstats.t) ->
        compare b.flips_per_second a.flips_per_second)
      status.stats
  in
  if stats <> [] then begin
    Buffer.add_string buf "Connected clients:\n";
    let max_len =
      List.fold_left
        (fun acc (s : Pb.Coinstats.t) ->
          max acc (String.length (commify s.flips_per_second)))
        0 stats
    in
    List.iter
      (fun (s : Pb.Coinstats.t) ->
        let speed = commify s.flips_per_second in
        Buffer.add_string buf
          (Printf.sprintf "%08x: %*s cps\n"
             (s.hash land 0xFFFFFFFF)
             max_len speed))
      stats;
    Buffer.add_char buf '\n'
  end;
  (* Milestone calculation *)
  if status.total_flips > 0 && status.flips_per_second > 0.0 then begin
    let milestone = log10 (float_of_int status.total_flips) in
    let rest =
      10.0 ** ceil milestone -. float_of_int status.total_flips
    in
    let remaining = rest /. status.flips_per_second in
    Buffer.add_string buf
      (Printf.sprintf "Time remaining to next milestone: %s\n\n"
         (timeify (int_of_float remaining)))
  end;
  (* 4-column x 32-row table *)
  let columns =
    Array.init 4 (fun j ->
      Array.init 32 (fun i -> commify results.(i + 32 * j)))
  in
  let max_widths =
    Array.init 4 (fun j ->
      Array.fold_left
        (fun acc s -> max acc (String.length s))
        1 columns.(j))
  in
  for i = 0 to 31 do
    for j = 0 to 3 do
      let pos = i + 32 * j + 1 in
      Buffer.add_string buf
        (Printf.sprintf "%3d: %*s" pos max_widths.(j) columns.(j).(i));
      if j < 3 then Buffer.add_string buf "        "
    done;
    Buffer.add_char buf '\n'
  done;
  Buffer.contents buf
