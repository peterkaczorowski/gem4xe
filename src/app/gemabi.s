;;; ---------------------------------------------------------------------------
;;; gemabi.s -- an application's two entry points into gem4xe.
;;;
;;; The whole binding is a software interrupt.  Calypsi's __simple_call puts
;;; a far-pointer first argument in X (bank) : C (low 16 bits), and that is
;;; where gem4xe's COP handler (src/sys/abi.s) takes the parameter block
;;; from; the byte after COP says which of the two managers is wanted, with
;;; the ST's trap #2 values.  Nothing here knows an address of gem4xe's:
;;; the COP vector is in the OS's RAM shadow, which gem4xe fills.
;;; ---------------------------------------------------------------------------
              .rtmodel version, "1"
              .rtmodel core, "*"
              .public vdi_call, aes_call

              .section farcode
vdi_call:     cop     #0x73
              rtl
aes_call:     cop     #0xc8
              rtl
