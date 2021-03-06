(*
 * Copyright (C) 2006-2009 Citrix Systems Inc.
 * Copyright (C) 2010 Anil Madhavapeddy <anil@recoil.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *)

open Lwt

type chan = {
  mutable page: Cstruct.t;
  mutable evtchn: Eventchn.t;
}
(* An inter-domain client is always via a shared memory page
   and an event channel. *)

external xenstore_start_page: unit -> Io_page.t = "caml_xenstore_start_page"

let open_channel () =
  let page = Io_page.to_cstruct (xenstore_start_page ()) in
  Xenstore_ring.Ring.init page;
  let evtchn = Eventchn.of_int Start_info.((get ()).store_evtchn) in
  return { page; evtchn }

let t = ref None
(* We keep a reference to any currently open xenstore connection
   for use in the suspend code. *)

let h = Eventchn.init ()

module IO = struct
    type 'a t = 'a Lwt.t
    type channel = chan
    let return = Lwt.return
    let (>>=) = Lwt.bind
    exception Already_connected
    exception Cannot_destroy

    let create () =
       match !t with
       | Some ch ->
         (* This can never happen provided only one xenstore client is
            ever created, see the function 'make' below. *)
         Console.log "ERROR: Already connected to xenstore: cannot reconnect";
         fail Already_connected
       | None ->
         lwt ch = open_channel () in
         t := Some ch;
         return ch

    let destroy t =
      Console.log "ERROR: It's not possible to destroy the default xenstore connection";
      fail Cannot_destroy

    (* XXX: unify with ocaml-xenstore-xen/xen/lib/xs_transport_domain *)
    let rec read t buf ofs len =
      let n = Xenstore_ring.Ring.Front.unsafe_read t.page buf ofs len in
      if n = 0 then begin
        lwt () = Activations.wait t.evtchn in
        read t buf ofs len
      end else begin
        Eventchn.notify h t.evtchn;
        return n
      end

    (* XXX: unify with ocaml-xenstore-xen/xen/lib/xs_transport_domain *)
    let rec write t buf ofs len =
      let n = Xenstore_ring.Ring.Front.unsafe_write t.page buf ofs len in
      if n > 0 then Eventchn.notify h t.evtchn;
      if n < len then begin
        lwt () = Activations.wait t.evtchn in
        write t buf (ofs + n) (len - n)
      end else return ()
end

module Client = Xs_client_lwt.Client(IO)

include Client

let client_cache = ref None
(* The whole application must only use one xenstore client, which will
   multiplex all requests onto the same ring. *)

let client_cache_m = Lwt_mutex.create ()
(* Multiple threads will call 'make' in parallel. We must ensure only
   one client is created. *)

let make () =
  Lwt_mutex.with_lock client_cache_m
    (fun () -> match !client_cache with
      | Some c -> return c
      | None ->
        lwt c = make () in
        client_cache := Some c;
        return c
    )

let resume client =
	lwt ch = open_channel () in
	begin match !t with
		| Some ch' ->
			ch'.page <- ch.page;
			ch'.evtchn <- ch.evtchn;
		| None ->
			();
	end;
	resume client
