/* B11 -- a struct assignment between a near object and a far one is an
 * internal compiler error when the struct is more than 8 bytes:
 *
 *   internal error: labeling failed
 *
 * Either direction (a near local, global or pointer target to or from a
 * far pointer target), every -O level, both data models.  Up to 8 bytes
 * the copy goes through registers and compiles; from 9 it is a block
 * move the back end cannot label.  Near-to-near and far-to-far copies
 * of any size compile.  The sources copy such a struct byte by byte
 * (fn_copy, src/desk/deskwin.c) or keep both sides far. */
typedef struct { short f_obid, f_flags, f_attr; unsigned short f_time, f_date;
                 long f_size; char f_name[14]; } FNODE;

void r_b11_bug(FNODE __far *pf, const FNODE *ps)
{
    *pf = *ps;
}
