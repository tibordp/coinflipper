module Pb = Coinflipper.Coinflipper

let format (status : Pb.Coinstatus.t) =
  let buf = Buffer.create 512 in
  Buffer.add_string buf
    "# HELP coinflipper_total_flips Total number of coins flipped\n";
  Buffer.add_string buf "# TYPE coinflipper_total_flips counter\n";
  Buffer.add_string buf
    (Printf.sprintf "coinflipper_total_flips %d\n" status.total_flips);
  Buffer.add_char buf '\n';
  Buffer.add_string buf
    "# HELP coinflipper_connected_clients Number of connected clients\n";
  Buffer.add_string buf "# TYPE coinflipper_connected_clients gauge\n";
  Buffer.add_string buf
    (Printf.sprintf "coinflipper_connected_clients %d\n"
       (List.length status.stats));
  Buffer.contents buf
