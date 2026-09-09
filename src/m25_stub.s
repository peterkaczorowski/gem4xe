;;; m25_stub.s -- the one symbol the interrupt vectors reach that this
;;; milestone has no use for.
;;;
;;; src/sys/irq.s points the native-mode COP vector at gem_cop, which is
;;; the ABI's entry (src/sys/abi.s) -- an application's way into the AES.
;;; A VDI milestone has no application and no AES, so linking abi.o would
;;; drag in the whole of both.  A COP here is a program that has gone
;;; wrong; returning is as good an answer as any.
              .rtmodel version, "1"
              .rtmodel cpu, "65816"
              .section code
              .public gem_cop
gem_cop:      rti
              .end
