;;; ---------------------------------------------------------------------------
;;; gemabi.s -- an application's three entry points into gem4xe.
;;;
;;; The whole binding is a software interrupt.  Calypsi's __simple_call puts
;;; a far-pointer first argument in X (bank) : C (low 16 bits), and that is
;;; where gem4xe's COP handler (src/sys/abi.s) takes the parameter block
;;; from; the byte after COP says which manager is wanted, with the ST's trap
;;; numbers: #$73 and #$C8 are trap #2's VDI and AES, #$01 is trap #1,
;;; GEMDOS.  Nothing here knows an address of gem4xe's:
;;; the COP vector is in the OS's RAM shadow, which gem4xe fills.
;;; ---------------------------------------------------------------------------
              .rtmodel version, "1"
              .rtmodel core, "*"
              .public vdi_call, aes_call, dos_call

              .section farcode
vdi_call:     cop     #0x73
              rtl
aes_call:     cop     #0xc8
              rtl
dos_call:     cop     #0x01
              rtl
