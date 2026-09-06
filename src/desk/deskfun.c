/* deskfun.c -- what the File menu does to files: a new folder, and
 * delete.
 *
 * The shape is the donor's deskfun.c (fun_mkdir, fun_del) and
 * deskdir.c (d_doop): one path buffer, mutated in place as the walk
 * goes down and back up, and a DTA per level.  Our GEMDOS keeps a
 * search's state in a slot owned by the DTA that started it
 * (src/sys/gemdos.c), exactly as the ST keeps it in the DTA itself, so
 * a folder inside a folder is a nested Fsfirst/Fsnext and the outer
 * search survives it -- provided each level sets its own DTA, which is
 * what the donor allocates one for.  MAX_DELLEVEL of them are in the
 * far arena (deskwin.c); the walk refuses to go deeper, as the donor
 * does at eight.
 *
 * A delete counts first and then deletes, so the dialog can say what
 * it is about to do and count down as it does it -- the donor's
 * OP_COUNT pass followed by OP_DELETE, with its ADCPALER dialog.
 */
#include "desk.h"

/* The path an operation works on: "A:\SUB\*.*", the "*.*" replaced by
 * the name being worked on and put back afterwards.  One, and near,
 * because an operation is never nested and the stack is the desktop's
 * scarcest memory (docs/phase14.md, milestone 6). */
static char op_path[LEN_ZPATH];

/* -- what the desktop says --------------------------------------------- */

/* An alert whose text is a free string of DESKTOP.RSC, by index (the
 * donor's fun_alert).  No string a person reads is in the C: a literal
 * cannot be translated, and the resource is where a translator can
 * reach it -- rsrc_gaddr answers a bank-$00 address, which is what
 * form_alert wants (docs/shipping.md).  The one exception is the alert
 * that says the resource is missing (desktop.c). */
WORD fun_alert(WORD defbut, WORD stnum)
{
    char *str;

    rsrc_gaddr(R_STRING, stnum, (void **)&str);
    return form_alert(defbut, str);
}

/* -- strings ----------------------------------------------------------- */

static char *put_str(char *d, const char *s)
{
    while ((*d = *s++) != 0)
        d++;
    return d;
}

/* Two paths, the same or not.  The listings and the specs are the
 * DOS's own upper case, so a plain comparison is the right one. */
static WORD same_path(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (WORD)(*a == *b);
}

static char *put_far(char *d, const char __far *s)
{
    while ((*d = *s++) != 0)
        d++;
    return d;
}

/* -- the dialogs' fields (the donor's deskinf.c) ------------------------ */

static TEDINFO *ted_of(OBJECT *tree, WORD obj)
{
    return (TEDINFO *)(uint16_t)tree[obj].ob_spec;
}

static void inf_sset(OBJECT *tree, WORD obj, const char *str)
{
    TEDINFO *ted = ted_of(tree, obj);
    char *text = (char *)(uint16_t)ted->te_ptext;
    WORD len = ted->te_txtlen;                  /* its own local, then the
                                                 * arithmetic: cc65816 drops
                                                 * the load otherwise
                                                 * (tools/ccbug, rule 5) */
    WORD n = (WORD)(len - 1);

    while (n-- > 0 && *str)
        *text++ = *str++;
    *text = 0;
}

static void inf_sget(OBJECT *tree, WORD obj, char *str)
{
    put_str(str, (const char *)(uint16_t)ted_of(tree, obj)->te_ptext);
}

/* The number at the right of the field, the donor's "%*lu". */
static void inf_numset(OBJECT *tree, WORD obj, LONG value)
{
    TEDINFO *ted = ted_of(tree, obj);
    char *text = (char *)(uint16_t)ted->te_ptext;
    WORD len = ted->te_txtlen;                  /* rule 5, as above: the
                                                 * field into a local, then
                                                 * the subtraction */
    WORD i = (WORD)(len - 1);

    text[i] = 0;
    do {
        text[--i] = (char)('0' + (WORD)(value % 10));
        value /= 10;
    } while (value && i > 0);
    while (i > 0)
        text[--i] = ' ';
}

/* Which of the two buttons after ok is selected: 0 for it, 1 for the
 * next (the donor's inf_what through inf_gindex), the state cleared. */
static WORD inf_what(OBJECT *tree, WORD ok)
{
    WORD i;

    for (i = 0; i < 2; i++)
        if (tree[ok + i].ob_state & SELECTED) {
            tree[ok + i].ob_state = NORMAL;
            return (WORD)(i == 0);
        }
    return 0;
}

/* -- the dialog on the screen ------------------------------------------ */

static GRECT dlg;

static void fun_start(OBJECT *tree)
{
    form_center(tree, &dlg.g_x, &dlg.g_y, &dlg.g_w, &dlg.g_h);
    form_dial(FMD_START, 0, 0, 0, 0, dlg.g_x, dlg.g_y, dlg.g_w, dlg.g_h);
    objc_draw(tree, ROOT, MAX_DEPTH, dlg.g_x, dlg.g_y, dlg.g_w, dlg.g_h);
}

static void fun_end(void)
{
    form_dial(FMD_FINISH, 0, 0, 0, 0, dlg.g_x, dlg.g_y, dlg.g_w, dlg.g_h);
}

/* One field of a dialog already on the screen (the donor's draw_fld). */
static void fun_fld(OBJECT *tree, WORD obj)
{
    GRECT t;

    objc_offset(tree, obj, &t.g_x, &t.g_y);
    t.g_w = tree[obj].ob_width;
    t.g_h = tree[obj].ob_height;
    objc_draw(tree, obj, MAX_DEPTH, t.g_x, t.g_y, t.g_w, t.g_h);
}

/* -- the path, down and back up (the donor's deskdir.c) ---------------- */

/* The last component of the path: "A:\SUB\*.*" -> the "*.*". */
static char *path_tail(char *path)
{
    char *p = path, *last = path;

    while (*p)
        if (*p++ == '\\')
            last = p;
    return last;
}

/* "A:\SUB\*.*" -> "A:\SUB\NAME", and the tail to put "*.*" back at. */
static char *add_fname(char *path, const char __far *name)
{
    char *tail = path_tail(path);

    put_far(tail, name);
    return tail;
}

static void set_all_files(char *tail)
{
    put_str(tail, "*.*");
}

/* "A:\SUB\*.*" -> "A:\SUB\NAME\*.*", FALSE if that will not fit. */
static WORD add_path(char *path, const char __far *name)
{
    char *tail = path_tail(path);
    char *end = put_far(tail, name);

    if ((WORD)(end - path) + 5 > LEN_ZPATH) {
        set_all_files(tail);
        return FALSE;
    }
    *end++ = '\\';
    set_all_files(end);
    return TRUE;
}

/* ...and back: "A:\SUB\NAME\*.*" -> "A:\SUB\*.*". */
static void sub_path(char *path)
{
    char *p = path_tail(path);

    if (p - path > 3) {                         /* not "A:\" itself */
        p--;                                    /* over the separator */
        while (p > path && p[-1] != '\\')
            p--;
        set_all_files(p);
    }
}

/* -- the walk ----------------------------------------------------------- */

/* One of the dialog's two counters, ticked down as the walk goes. */
static void op_count(WORD field, LONG left)
{
    inf_numset(G.a_delete, field, left);
    fun_fld(G.a_delete, field);
}

/* Delete one file, alerting when the DOS refuses. */
static WORD del_file(void)
{
    if (Fdelete(op_path) == E_OK)
        return TRUE;
    fun_alert(1, STDELFIL);
    return FALSE;
}

/* -- copying and moving (the donor's deskfun.c and deskdir.c) ---------- */

/* Where the operation is going: the same shape as op_path, kept in step
 * with it as the walk goes down and back up.  A copy is two paths, and
 * that is the only difference between it and the delete above. */
static char dst_path[LEN_ZPATH];

/* The directory dst_path names, without the "*.*" on the end.  Made
 * when a copy walks into a folder that is not there yet; a folder that
 * IS there is not an error -- copying over an existing tree is what a
 * second copy of the same thing means. */
static WORD make_dir(void)
{
    char *tail = path_tail(dst_path);
    LONG ret;

    /* "A:\\DIR\\*.*" -> "A:\\DIR": the separator goes as well, since a
     * DOS takes a directory by its name and not as a path to nothing. */
    tail[-1] = 0;
    ret = Dcreate(dst_path);
    tail[-1] = '\\';
    set_all_files(tail);
    if (ret == E_OK || ret == EACCDN)        /* it was already there */
        return TRUE;
    fun_alert(1, STFOFAIL);
    return FALSE;
}

/* One file, op_path to dst_path, through the arena's buffer.  A copy
 * that stops half way leaves nothing behind: a truncated file looks
 * like a whole one to everything that reads it afterwards. */
static WORD copy_file(void)
{
    LONG in, out, got, put;
    char __far *buf = G.g_copybuf;
    WORD ok = TRUE;

    in = Fopen(op_path, 0)      /* read */;
    if (in < 0) {
        fun_alert(1, STCPYFIL);
        return FALSE;
    }
    out = Fcreate(dst_path, 0);
    if (out < 0) {
        Fclose((WORD)in);
        fun_alert(1, STCPYFIL);
        return FALSE;
    }
    for (;;) {
        got = Fread((WORD)in, (LONG)COPY_BUF, buf);
        if (got <= 0) {
            if (got < 0)
                ok = FALSE;
            break;
        }
        put = Fwrite((WORD)out, got, buf);
        if (put != got) {
            fun_alert(1, STDISKFU);
            ok = FALSE;
            break;
        }
        if (got < (LONG)COPY_BUF)               /* that was the end of it */
            break;
    }
    Fclose((WORD)in);
    Fclose((WORD)out);
    if (!ok) {
        Fdelete(dst_path);          /* half a file is worse than none */
        return FALSE;
    }
    return TRUE;
}

/* The file at op_path, copied to dst_path and -- for a move -- taken
 * away afterwards.  A move is a copy and a delete on this machine:
 * neither DOS renames a file into another directory, and the two
 * drives of a floppy machine are not the same drive anyway. */
static WORD copy_one(WORD op)
{
    if (!copy_file())
        return FALSE;
    if (op == OP_MOVE && !del_file())
        return FALSE;
    return TRUE;
}

/* One level of the walk.  op_path ends in "*.*", and for a copy or a
 * move dst_path does too, kept in step with it step for step.
 *
 *   OP_COUNT    add the entries up, so the dialog can say what it will do
 *   OP_DELETE   delete them, a folder after its contents
 *   OP_COPY     copy them, a folder BEFORE its contents (it has to be
 *               there to copy into)
 *   OP_MOVE     the same, and take the original away behind it
 *
 * FALSE when something stopped it, and then the paths are left where
 * they stopped -- the caller is stopping too. */
static WORD walk(WORD level, WORD op)
{
    DTA __far *dta;
    LONG ret;
    char *tail, *dtail;

    if (level >= MAX_DELLEVEL) {
        fun_alert(1, STFO8DEE);
        return FALSE;
    }
    dta = &G.g_opdta[level];
    Fsetdta(dta);
    ret = Fsfirst(op_path, FA_SUBDIR);
    while (ret == E_OK) {
        if (dta->d_fname[0] != '.') {           /* "." and ".." */
            if (dta->d_attrib & FA_SUBDIR) {
                if (!add_path(op_path, dta->d_fname)) {
                    fun_alert(1, STDEEPPA);
                    return FALSE;
                }
                if (op == OP_COPY || op == OP_MOVE) {
                    if (!add_path(dst_path, dta->d_fname)) {
                        fun_alert(1, STDEEPPA);
                        return FALSE;
                    }
                    if (!make_dir())
                        return FALSE;
                }
                if (!walk((WORD)(level + 1), op))
                    return FALSE;
                sub_path(op_path);
                if (op == OP_COPY || op == OP_MOVE)
                    sub_path(dst_path);
                Fsetdta(dta);                   /* this level's own again */
                tail = add_fname(op_path, dta->d_fname);
                if (op == OP_COUNT) {
                    G.g_ndirs++;
                } else if (op == OP_COPY) {
                    G.g_ndirs--;
                    op_count(CDFOLDS, G.g_ndirs);
                } else {                        /* delete, and move's tail */
                    if (Ddelete(op_path) != E_OK) {
                        fun_alert(1, STDELDIR);
                        return FALSE;
                    }
                    G.g_ndirs--;
                    op_count(CDFOLDS, G.g_ndirs);
                }
                set_all_files(tail);
            } else {
                tail = add_fname(op_path, dta->d_fname);
                if (op == OP_COUNT) {
                    G.g_nfiles++;
                } else if (op == OP_DELETE) {
                    if (!del_file())
                        return FALSE;
                    G.g_nfiles--;
                    op_count(CDFILES, G.g_nfiles);
                } else {
                    dtail = add_fname(dst_path, dta->d_fname);
                    if (!copy_one(op))
                        return FALSE;
                    set_all_files(dtail);
                    G.g_nfiles--;
                    op_count(CDFILES, G.g_nfiles);
                }
                set_all_files(tail);
            }
        }
        ret = Fsnext();
    }
    return TRUE;
}

/* -- the two operations ------------------------------------------------- */

/* The dialog serves all three operations and says which one it is from
 * a string of the resource -- a title in the C could not be translated
 * (docs/shipping.md).  The donor has a dialog each; one and a title is
 * the same dialog with less of DESKTOP.RSC spent on it. */
static void op_title(WORD op)
{
    char *str;
    WORD stnum = (op == OP_COPY) ? STCPYTTL
               : (op == OP_MOVE) ? STMOVTTL : STDELTTL;

    rsrc_gaddr(R_STRING, stnum, (void **)&str);
    G.a_delete[CDTITLE].ob_spec = (LONG)(uint32_t)(char __far *)str;
}

/* Where a drop landed, as a path ending in "*.*": a window's own
 * directory, a folder in one, or a drive's root on the desk.  FALSE if
 * it landed on nothing that can hold a file. */
static WORD drop_path(WORD dst_wh, WORD dst_obj, char *path)
{
    WNODE *pd;
    FNODE __far *pf;

    if (dst_wh == DESKWH) {
        WORD drive = (WORD)(obj_info(dst_obj)->icon.ib_char & 0xFF);

        if (!drive)
            return FALSE;                       /* the trash: not a place */
        path[0] = (char)drive;
        path[1] = ':';
        path[2] = '\\';
        path[3] = 0;
        set_all_files(path + 3);
        return TRUE;
    }
    pd = win_find(dst_wh);
    if (!pd)
        return FALSE;
    put_str(path, pd->w_path.p_spec);
    if (!dst_obj)                               /* the window's own directory */
        return TRUE;
    pf = win_fnode(pd, dst_obj);
    if (!pf || !(pf->f_attr & FA_SUBDIR))       /* onto a file: no */
        return FALSE;
    return add_path(path, pf->f_name);
}

/* An item dragged out of pw and let go somewhere.  SHIFT makes it a
 * move: the ST does that with CONTROL, which POKEY cannot see unless a
 * key is down with it (SKSTAT reports the shift key alone and nothing
 * else), so the one modifier this machine can be asked about while the
 * button is held is the one it uses.
 *
 * The shape is fun_del's, because it is the same three passes: count
 * what is selected, ask, then do it with the counters ticking down. */
void fun_file2any(WNODE *pw, WORD dst_wh, WORD dst_obj, WORD kstate)
{
    OBJECT *tree = G.a_delete;
    FNODE __far *pf;
    char dest[LEN_ZPATH];
    WORD i, ok, op, chose = FALSE;

    op = (kstate & (MODE_LSHIFT | MODE_RSHIFT)) ? OP_MOVE : OP_COPY;
    if (dst_wh == DESKWH && !(obj_info(dst_obj)->icon.ib_char & 0xFF)) {
        fun_del(pw);                            /* the trash */
        return;
    }
    if (!drop_path(dst_wh, dst_obj, dest))
        return;

    pf = pw->w_path.p_flist;
    for (i = 0; i < pw->w_path.p_count; i++, pf++)
        if (pf->f_flags & F_SELECTED)
            chose = TRUE;
    if (!chose) {
        fun_alert(1, STNOTHIN);
        return;
    }
    if (same_path(dest, pw->w_path.p_spec)) {   /* where it already is */
        fun_alert(1, STSAMEPL);
        return;
    }

    desk_busy(TRUE);                            /* what it will do */
    G.g_nfiles = 0;
    G.g_ndirs = 0;
    ok = TRUE;
    pf = pw->w_path.p_flist;
    for (i = 0; i < pw->w_path.p_count && ok; i++, pf++) {
        if (!(pf->f_flags & F_SELECTED))
            continue;
        put_str(op_path, pw->w_path.p_spec);
        if (pf->f_attr & FA_SUBDIR) {
            if (!add_path(op_path, pf->f_name)) {
                fun_alert(1, STDEEPPA);
                ok = FALSE;
                break;
            }
            ok = walk(0, OP_COUNT);
            G.g_ndirs++;
        } else {
            G.g_nfiles++;
        }
    }
    desk_busy(FALSE);
    if (!ok)
        return;

    op_title(op);                               /* ...and ask */
    inf_numset(tree, CDFILES, G.g_nfiles);
    inf_numset(tree, CDFOLDS, G.g_ndirs);
    fun_start(tree);
    form_do(tree, 0);
    ok = inf_what(tree, CDOK);
    if (!ok) {
        fun_end();
        op_title(OP_DELETE);
        return;
    }

    desk_busy(TRUE);                            /* ...and do it */
    pf = pw->w_path.p_flist;
    for (i = 0; i < pw->w_path.p_count && ok; i++, pf++) {
        char *tail, *dtail;

        if (!(pf->f_flags & F_SELECTED))
            continue;
        put_str(op_path, pw->w_path.p_spec);
        put_str(dst_path, dest);
        if (pf->f_attr & FA_SUBDIR) {
            add_path(op_path, pf->f_name);      /* both fitted at the count */
            if (!add_path(dst_path, pf->f_name) || !make_dir())
                break;
            ok = walk(0, op);
            if (!ok)
                break;
            sub_path(op_path);
            sub_path(dst_path);
            tail = add_fname(op_path, pf->f_name);
            if (op == OP_MOVE && Ddelete(op_path) != E_OK) {
                fun_alert(1, STDELDIR);
                break;
            }
            G.g_ndirs--;
            op_count(CDFOLDS, G.g_ndirs);
            set_all_files(tail);
        } else {
            tail = add_fname(op_path, pf->f_name);
            dtail = add_fname(dst_path, pf->f_name);
            if (!copy_one(op))
                break;
            set_all_files(dtail);
            set_all_files(tail);
            G.g_nfiles--;
            op_count(CDFILES, G.g_nfiles);
        }
    }
    desk_busy(FALSE);
    fun_end();
    op_title(OP_DELETE);                        /* as the resource has it */
    win_rebld(pw);
    {                                           /* and the other end of it */
        WNODE *pd = (dst_wh == DESKWH) ? 0 : win_find(dst_wh);
        if (pd && pd != pw)
            win_rebld(pd);
    }
}

/* File -> New folder: the name in a dialog, Dcreate, the window read
 * again.  Nothing is returned: an operation on files never ends the
 * desktop's loop -- only a program run from an icon does (do_open). */
void fun_mkdir(WNODE *pw)
{
    OBJECT *tree = G.a_mkdir;
    char name[LEN_ZFNAME], made[LEN_ZFNAME];
    char *tail;
    WORD i, j;

    put_str(op_path, pw->w_path.p_spec);
    inf_sset(tree, MKNAME, "");
    fun_start(tree);
    form_do(tree, 0);
    fun_end();
    if (!inf_what(tree, MKOK))                  /* Cancel */
        return;

    inf_sget(tree, MKNAME, name);
    for (i = 0, j = 0; name[i] && i < 8; i++)   /* the donor's unfmt_str: */
        if (name[i] != ' ')                     /* the places, then a dot */
            made[j++] = name[i];                /* before the extension */
    if (name[i]) {
        made[j++] = '.';
        while (name[i])
            made[j++] = name[i++];
    }
    made[j] = 0;
    if (!j)
        return;

    tail = path_tail(op_path);
    put_str(tail, made);
    desk_busy(TRUE);
    i = (WORD)(Dcreate(op_path) == E_OK);
    desk_busy(FALSE);
    if (!i) {
        fun_alert(1, STFOFAIL);
        return;
    }
    win_rebld(pw);
}

/* File -> Delete: what is selected in the window, counted, confirmed
 * and then deleted, folders and all. */
void fun_del(WNODE *pw)
{
    OBJECT *tree = G.a_delete;
    FNODE __far *pf;
    WORD i, ok;

    ok = FALSE;
    pf = pw->w_path.p_flist;
    for (i = 0; i < pw->w_path.p_count; i++, pf++)
        if (pf->f_flags & F_SELECTED)
            ok = TRUE;
    if (!ok)                                    /* nothing chosen */
        return;

    desk_busy(TRUE);                            /* what it will do (OP_COUNT) */
    G.g_nfiles = 0;
    G.g_ndirs = 0;
    pf = pw->w_path.p_flist;
    for (i = 0; i < pw->w_path.p_count && ok; i++, pf++) {
        if (!(pf->f_flags & F_SELECTED))
            continue;
        put_str(op_path, pw->w_path.p_spec);
        if (pf->f_attr & FA_SUBDIR) {
            if (!add_path(op_path, pf->f_name)) {
                fun_alert(1, STDEEPPA);
                ok = FALSE;
                break;
            }
            ok = walk(0, OP_COUNT);
            G.g_ndirs++;
        } else {
            G.g_nfiles++;
        }
    }
    desk_busy(FALSE);
    if (!ok)
        return;

    inf_numset(tree, CDFILES, G.g_nfiles);      /* ...and ask */
    inf_numset(tree, CDFOLDS, G.g_ndirs);
    fun_start(tree);
    form_do(tree, 0);
    ok = inf_what(tree, CDOK);
    if (!ok) {
        fun_end();
        return;
    }

    desk_busy(TRUE);                            /* ...and do it */
    pf = pw->w_path.p_flist;
    for (i = 0; i < pw->w_path.p_count && ok; i++, pf++) {
        char *tail;

        if (!(pf->f_flags & F_SELECTED))
            continue;
        put_str(op_path, pw->w_path.p_spec);
        if (pf->f_attr & FA_SUBDIR) {
            add_path(op_path, pf->f_name);      /* it fitted at the count */
            ok = walk(0, OP_DELETE);
            if (!ok)
                break;
            sub_path(op_path);
            tail = add_fname(op_path, pf->f_name);
            if (Ddelete(op_path) != E_OK) {
                fun_alert(1, STDELDIR);
                break;
            }
            G.g_ndirs--;
            inf_numset(tree, CDFOLDS, G.g_ndirs);
            fun_fld(tree, CDFOLDS);
            set_all_files(tail);
        } else {
            add_fname(op_path, pf->f_name);
            if (!del_file())
                break;
            G.g_nfiles--;
            inf_numset(tree, CDFILES, G.g_nfiles);
            fun_fld(tree, CDFILES);
        }
    }
    desk_busy(FALSE);
    fun_end();
    win_rebld(pw);
}
