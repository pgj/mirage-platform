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

external block_kernel : float -> unit = "caml_block_kernel"

let enter_hooks = Lwt_sequence.create ()
let exit_hooks  = Lwt_sequence.create ()

let rec call_hooks hooks =
  match Lwt_sequence.take_opt_l hooks with
    | None -> return ()
    | Some f ->
      let _ =
        try_lwt
          f ()
        with exn ->
         let t   = Printexc.to_string exn in
         let msg = Printf.sprintf "Exception in a hook: \"%s\"!" t in
         prerr_endline msg;
         return ()
      in call_hooks hooks

let run t =
  let t = call_hooks enter_hooks <&> t in
  let rec aux () =
    Lwt.wakeup_paused ();
    Time.restart_threads Clock.time;
    try
      match Lwt.poll t with
      | Some _ -> true
      | None   ->
        let timeout =
          match Time.select_next Clock.time with
          | None    -> 86400000.000
          | Some tm -> tm
        in
        block_kernel timeout;
        false
    with exn ->
      (let t   = Printexc.to_string exn in
       let msg = Printf.sprintf "Top-level exception: \"%s\"!" t in
       prerr_endline msg;
      true)
  in
  let finalize () =
    Lwt.cancel t;
    Gc.compact ()
  in
  ignore (Callback.register "OS.Main.run" aux);
  ignore (Callback.register "OS.Main.finalize" finalize)

let () = at_exit (fun () -> run (call_hooks exit_hooks))
let at_enter f = ignore (Lwt_sequence.add_l f enter_hooks)
let at_exit  f = ignore (Lwt_sequence.add_l f exit_hooks)
