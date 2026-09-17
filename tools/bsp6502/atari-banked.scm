;;; A THROWAWAY Atari map, to find out one thing: what does a call from
;;; resident bank-$00 code into a function the linker put in a banked
;;; window actually compile to?
;;;
;;; The machine: an XL/XE with a PORTB RAM expansion.  The only
;;; switchable window is $4000-$7FFF, 16 KB, and $2000-$3FFF is where
;;; resident code and data live.  That is the shape the Commander X16's
;;; cx16-banked.scm has too, with a smaller slot at $A000.
(define memories
  '((memory ZeroPage  (address (#x80 . #xff)) (type ram) (qualifier zpage)
            (section (registers #x80)))
    (memory Stack     (address (#x100 . #x1ff)) (type ram))
    ;; resident: bank $00, what is always mapped
    (memory Resident  (address (#x2000 . #x3fff)) (type any)
            (section startup code data cdata idata switch data_init_table))
    ;; the PORTB window, and the banks that scatter through it
    (memory BankSlot  (address (#x4000 . #x7fff))
            (scatter-to RAM-banks)
            :generate-instances
            (section bankedcode))
    (memory BankedRAM (address (#x10000 . #xfffff))
            (section RAM-banks))
    (memory Screen    (address (#x8000 . #x9bff)) (type ram))
    ))
