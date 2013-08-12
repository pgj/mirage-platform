(*-
 * Copyright (c) 2012, 2013 Gabor Pali
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *)

open Lwt
open Printf

type t = {
    backend_id: int;
    backend: string;
    mac: string;
    mutable active: bool;
}

type id = string

let id_of_string s = s
let string_of_id i = i

let id t = t.backend

external get_vifs: unit -> id list = "caml_get_vifs"
external plug_vif: id -> bool * int * string = "caml_plug_vif"
external unplug_vif: id -> unit = "caml_unplug_vif"
external get_mbufs     : int -> Cstruct.t list = "caml_get_mbufs"
external get_next_mbuf : int -> Cstruct.t option = "caml_get_next_mbuf"
external put_mbufs     : int -> Cstruct.t list -> unit = "caml_put_mbufs"

let devices : (id, t) Hashtbl.t = Hashtbl.create 1

let enumerate () =
  let vifs = get_vifs () in
  let rec read_vif l acc = match l with
    | []      -> return acc
    | (x::xs) ->
      lwt sid = return x in
      read_vif xs (sid :: acc)
  in
  read_vif vifs []

let plug id =
  try
    return (Hashtbl.find devices id)
  with Not_found ->
    let backend = id in
    let active,backend_id,mac = plug_vif id in
    let t = { backend_id; backend; mac; active } in
    Hashtbl.add devices id t;
    return t

let unplug id =
 try
   let t = Hashtbl.find devices id in
   t.active <- false;
   Hashtbl.remove devices id;
   unplug_vif id
 with Not_found -> ()

let create () =
  lwt ids = enumerate () in
  Lwt.catch
    (fun () -> Lwt_list.map_p plug ids)
    (fun exn -> Hashtbl.iter (fun id _ -> unplug id) devices; Lwt.fail exn)

let writev ifc bufs =
  put_mbufs (ifc.backend_id) bufs;
  return ()

let write ifc buf = writev ifc [buf]

let rec input ifc =
  let next = get_next_mbuf ifc.backend_id in
  match next with
  | None       ->
    Time.yield () >>
    input ifc
  | Some frame -> return frame

let rec listen ifc fn =
  match ifc.active with
  | true ->
    begin
      try_lwt
        lwt frame = input ifc in
        fn frame;
        Time.yield () >>
        listen ifc fn
      with exn ->
        return (printf "EXN: %s, bt: %s\n%!"
          (Printexc.to_string exn) (Printexc.get_backtrace ()));
        listen ifc fn
    end;
  | false -> return ()

let mac ifc =
  let s = String.create 6 in
  Scanf.sscanf ifc.mac "%02x:%02x:%02x:%02x:%02x:%02x"
    (fun a b c d e f ->
      s.[0] <- Char.chr a;
      s.[1] <- Char.chr b;
      s.[2] <- Char.chr c;
      s.[3] <- Char.chr d;
      s.[4] <- Char.chr e;
      s.[5] <- Char.chr f;
    );
  s

let ethid ifc = string_of_int ifc.backend_id

let get_writebuf ifc =
  let page = Io_page.get 1 in
  return (Cstruct.of_bigarray page)
