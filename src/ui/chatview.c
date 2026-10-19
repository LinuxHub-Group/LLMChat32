/*
 * chatview.c - conversation view (see chatview.h).
 *
 * Layout model: one full layout pass per repaint. The pass walks the
 * session top-down; for each message it joins the BLOCK_TEXT pieces
 * (no separator - Task 17 may append one block per streaming fragment,
 * which must flow as continuous text) and measures the result with
 * DrawTextW DT_CALCRECT (DT_WORDBREAK | DT_LEFT) at a maximum bubble
 * width of 72% of the client width, then records per-message geometry
 * (s_items) plus a flat list of link rects (s_links: think link and one
 * per image block, both for Task 18 hit-testing). Consecutive messages
 * stack with CV_GAP px gaps inside an 8px margin.
 *
 * Text conversion: block text is narrow UTF-8; the ui_* wrappers convert
 * through a 1024-unit stack buffer, too small for message bodies, so
 * bodies go out through the ui_drawtext_ww outlet after the view converts
 * them with utf8_to_utf16 (cap = strlen+1 units is an upper bound, so
 * conversion never truncates and never splits a surrogate pair). Each
 * layout item owns its converted copy until the next relayout; there is
 * deliberately no cross-layout per-block cache - session arrays realloc
 * on append, which moves Block pointers and would stale any
 * pointer-keyed cache. Short strings (links) still use ui_drawtext_w /
 * ui_text_width.
 *
 * Scrolling: s_scroll is the content-space y of the view top, clamped to
 * [0, max(0, content_h - view_h)]. s_pin_bottom re-pins the view to the
 * end on every relayout while set (cleared by scrolling away from the
 * bottom, restored by reaching it again or by chatview_scroll_bottom()).
 *
 * Background (Task 16): an optional image decoded through image.c and
 * pre-blended over white with draw_blend_white() at the configured
 * opacity into a 24-bit bottom-up DIB (s_bg_bmp). Painting order is
 * white fill -> background BitBlt anchored at the client bottom-right
 * corner (cw - w, ch - h; fixed to the control, never scroll-coupled,
 * no tiling) -> bubbles -> scrollbar. Re-setting frees and rebuilds the
 * bitmap; a missing path or a decode failure silently yields the plain
 * white background (a status-strip note was judged YAGNI).
 */
#include "chatview.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../core/image.h"
#include "../core/utf.h"
#include "draw.h"
#include "mainwin.h"
#include "strings.h"
#include "ui.h"

#define CV_CLASS   "LlmChat"
#define CV_MARGIN  8                 /* bubble to content edges          */
#define CV_GAP     8                 /* between messages                 */
#define CV_PAD_X   8                 /* text padding inside bubble       */
#define CV_PAD_Y   6
#define CV_MAXW_PCT 72               /* bubble max width, % of client    */
#define CV_SB_W    12                /* scrollbar width                  */
#define CV_LINE_PAD 4                /* link line height = lineh + this  */
#define CV_PART_GAP 2                /* between bubble content parts     */
#define CV_MIN_BUBBLE_W 40
#define CV_WHEEL_LINES 3

#define CV_LK_THINK 0
#define CV_LK_IMAGE 1

typedef struct {
    RECT bubble;      /* outer rect incl tail, content coords */
    RECT body;        /* roundrect area (bubble minus tail)   */
    RECT think;       /* think-link line rect, empty if none  */
    wchar_t *wtext;   /* converted joined text, NULL if empty */
    int has_think;
    int textw;        /* wrapped text size                    */
    int texth;
} CvItem;

typedef struct {
    RECT rc;          /* content coords */
    int kind;         /* CV_LK_THINK | CV_LK_IMAGE */
    int msg;          /* message index */
    int blk;          /* block index (image), -1 for think */
} CvLink;

static HWND    s_hwnd = NULL;
static int     s_registered = 0;
static Session *s_sess = NULL;
static int     s_scroll = 0;
static int     s_pin_bottom = 1;
static int     s_layout_valid = 0;
static int     s_content_h = 0;
static int     s_lineh = 16;      /* ui_font line height */

static CvItem *s_items = NULL;
static int    s_nitems = 0;

static CvLink *s_links = NULL;
static int    s_nlinks = 0;
static int    s_linkcap = 0;

static char    *s_tbuf = NULL;    /* scratch: joined UTF-8 text   */
static int     s_tcap = 0;        /* bytes */

static HDC     s_memdc = NULL;    /* double buffer                */
static HBITMAP s_bmp = NULL;
static HGDIOBJ s_oldbmp = NULL;
static int     s_bw = 0;
static int     s_bh = 0;

static HDC     s_bg_dc = NULL;    /* background DIB + its DC      */
static HBITMAP s_bg_bmp = NULL;
static HGDIOBJ s_bg_old = NULL;   /* stock bitmap from SelectObject */
static int     s_bg_w = 0;
static int     s_bg_h = 0;

static int     s_drag = 0;        /* scrollbar thumb drag state   */
static int     s_drag_off = 0;

/* ------------------------------------------------------------------ */
/* scratch buffer                                                      */

static char *cv_tbuf(int bytes)
{
    if (bytes > s_tcap) {
        char *nb = (char *)realloc(s_tbuf, (size_t)bytes);
        if (nb == NULL)
            return NULL;
        s_tbuf = nb;
        s_tcap = bytes;
    }
    return s_tbuf;
}

/* ------------------------------------------------------------------ */
/* background image (Task 16)                                          */

static void cv_bg_free(void)
{
    if (s_bg_dc != NULL) {
        if (s_bg_old != NULL)
            SelectObject(s_bg_dc, s_bg_old);
        DeleteDC(s_bg_dc);
        s_bg_dc = NULL;
        s_bg_old = NULL;
    }
    if (s_bg_bmp != NULL) {
        DeleteObject(s_bg_bmp);
        s_bg_bmp = NULL;
    }
    s_bg_w = 0;
    s_bg_h = 0;
}

/* Decode path, blend over white at opacity into a 24-bit bottom-up DIB
 * (BGR rows), keep it selected in s_bg_dc for paint-time BitBlt.
 * Returns 0 on success; on any failure the background stays plain white
 * (cv_bg_free() has already run, so s_bg_bmp is NULL). */
static int cv_bg_build(const char *path, int opacity)
{
    unsigned char *rgba = NULL;
    unsigned char *blend = NULL;
    unsigned char *bits;
    BITMAPINFO bi;
    HBITMAP bmp;
    int w, h, stride, x, y;

    if (image_decode_file(path, &w, &h, &rgba) != 0)
        return -1;
    blend = (unsigned char *)malloc((size_t)w * (size_t)h * 3u);
    if (blend == NULL) {
        image_free(rgba);
        return -1;
    }
    draw_blend_white(rgba, w * h, opacity, blend);  /* R,G,B rows */
    image_free(rgba);

    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;      /* positive: bottom-up DIB */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&bits,
                           NULL, 0);
    if (bmp == NULL || bits == NULL) {
        free(blend);
        if (bmp != NULL)
            DeleteObject(bmp);
        return -1;
    }
    stride = ((w * 3 + 3) / 4) * 4;
    for (y = 0; y < h; y++) {
        unsigned char *dst = bits + (size_t)(h - 1 - y) * (size_t)stride;
        const unsigned char *src = blend + (size_t)y * (size_t)w * 3u;

        for (x = 0; x < w; x++) {
            dst[x * 3 + 0] = src[x * 3 + 2];   /* B */
            dst[x * 3 + 1] = src[x * 3 + 1];   /* G */
            dst[x * 3 + 2] = src[x * 3 + 0];   /* R */
        }
    }
    free(blend);

    s_bg_dc = CreateCompatibleDC(NULL);
    if (s_bg_dc == NULL) {
        DeleteObject(bmp);
        return -1;
    }
    s_bg_old = SelectObject(s_bg_dc, bmp);
    s_bg_bmp = bmp;
    s_bg_w = w;
    s_bg_h = h;
    return 0;
}

/* ------------------------------------------------------------------ */
/* scroll arithmetic (view_h = client height)                          */

static int cv_view_h(void)
{
    RECT rc;

    if (s_hwnd == NULL)
        return 0;
    GetClientRect(s_hwnd, &rc);
    return rc.bottom;
}

static int cv_view_w(void)
{
    RECT rc;

    if (s_hwnd == NULL)
        return 0;
    GetClientRect(s_hwnd, &rc);
    return rc.right;
}

static int cv_max_scroll(void)
{
    int m = s_content_h - cv_view_h();

    return m > 0 ? m : 0;
}

static int cv_thumb_h(void)
{
    int ch = cv_view_h();

    if (s_content_h <= ch || s_content_h <= 0)
        return 0;
    {
        int t = ch * ch / s_content_h;
        if (t < 16)
            t = 16;
        if (t > ch)
            t = ch;
        return t;
    }
}

static int cv_thumb_y(void)
{
    int ch = cv_view_h();
    int th = cv_thumb_h();
    int ms = cv_max_scroll();

    if (ms <= 0 || th <= 0)
        return 0;
    return (s_scroll * (ch - th)) / ms;
}

static void cv_clamp_scroll(void)
{
    int ms = cv_max_scroll();

    if (s_scroll < 0)
        s_scroll = 0;
    if (s_scroll > ms)
        s_scroll = ms;
}

static void cv_scroll_to(int y)
{
    s_scroll = y;
    cv_clamp_scroll();
    s_pin_bottom = (s_scroll == cv_max_scroll()) && cv_max_scroll() > 0;
    InvalidateRect(s_hwnd, NULL, FALSE);
}

static void cv_scroll_by(int dy)
{
    cv_scroll_to(s_scroll + dy);
}

/* ------------------------------------------------------------------ */
/* layout                                                              */

static void layout_free(void)
{
    int i;

    if (s_items != NULL) {
        for (i = 0; i < s_nitems; i++)
            free(s_items[i].wtext);
        free(s_items);
        s_items = NULL;
    }
    s_nitems = 0;
    free(s_links);
    s_links = NULL;
    s_nlinks = 0;
    s_linkcap = 0;
    s_content_h = 0;
}

static int cv_add_link(const RECT *rc, int kind, int msg, int blk)
{
    if (s_nlinks >= s_linkcap) {
        int nc = s_linkcap == 0 ? 8 : s_linkcap * 2;
        CvLink *nl = (CvLink *)realloc(s_links, (size_t)nc * sizeof(CvLink));
        if (nl == NULL)
            return -1;
        s_links = nl;
        s_linkcap = nc;
    }
    s_links[s_nlinks].rc = *rc;
    s_links[s_nlinks].kind = kind;
    s_links[s_nlinks].msg = msg;
    s_links[s_nlinks].blk = blk;
    s_nlinks++;
    return 0;
}

/* Run the full layout pass. hdc must have ui_font() selected (it is
 * used for DT_CALCRECT measuring and link width measurement). */
static void layout_build(HDC hdc)
{
    RECT rc;
    TEXTMETRIC tm;
    int cw, maxw, y, i;

    GetClientRect(s_hwnd, &rc);
    cw = rc.right;
    if (GetTextMetrics(hdc, &tm))
        s_lineh = tm.tmHeight + tm.tmExternalLeading;

    s_nlinks = 0;

    maxw = (cw - CV_SB_W) * CV_MAXW_PCT / 100;
    if (maxw > cw - CV_SB_W - 2 * CV_MARGIN)
        maxw = cw - CV_SB_W - 2 * CV_MARGIN;
    if (maxw < CV_MIN_BUBBLE_W)
        maxw = CV_MIN_BUBBLE_W;

    if (s_sess == NULL || s_sess->nm <= 0 || cw < 2 * CV_MARGIN + CV_SB_W) {
        layout_free();   /* drop any stale layout from a previous session */
        s_layout_valid = 1;
        if (s_pin_bottom)
            s_scroll = 0;
        cv_clamp_scroll();
        return;
    }

    if (s_items != NULL) {
        int k;
        for (k = 0; k < s_nitems; k++)
            free(s_items[k].wtext);
        free(s_items);
    }
    s_items = (CvItem *)calloc((size_t)s_sess->nm, sizeof(CvItem));
    s_nitems = s_items != NULL ? s_sess->nm : 0;

    y = CV_MARGIN;
    for (i = 0; i < s_nitems; i++) {
        Msg *m = &s_sess->m[i];
        int left = (m->role != ROLE_USER);
        int j;
        int has_think = 0;
        int nimg = 0;
        int tlen = 0;
        int textw = 0, texth = 0;
        int think_h = 0, think_w = 0, img_w = 0, img_h;
        int contw, coth, body_h, outer_h, outer_w, x, gap, ty;
        CvItem *it;

        /* inventory blocks */
        for (j = 0; j < m->nb; j++) {
            if (m->b[j].kind == BLOCK_THINK)
                has_think = 1;
            else if (m->b[j].kind == BLOCK_IMAGE)
                nimg++;
            else if (m->b[j].text != NULL)
                tlen += (int)strlen(m->b[j].text);
        }

        /* join the visible text blocks and convert once; the wide copy
         * is owned by the item so painting needs no reconversion */
        if (tlen > 0) {
            char *tb = cv_tbuf(tlen + 1);
            wchar_t *wb;
            int off = 0;
            RECT mr;

            if (tb != NULL) {
                for (j = 0; j < m->nb; j++) {
                    if (m->b[j].kind == BLOCK_TEXT
                        && m->b[j].text != NULL) {
                        int bl = (int)strlen(m->b[j].text);
                        memcpy(tb + off, m->b[j].text, (size_t)bl);
                        off += bl;
                    }
                }
                tb[off] = '\0';
                wb = (wchar_t *)malloc((size_t)(off + 1)
                                       * sizeof(wchar_t));
                if (wb != NULL) {
                    if (utf8_to_utf16(tb, wb, off + 1) >= 0) {
                        mr.left = 0;
                        mr.top = 0;
                        mr.right = maxw - 2 * CV_PAD_X;
                        mr.bottom = 0;
                        ui_drawtext_ww(hdc, wb, &mr,
                                       DT_CALCRECT | DT_WORDBREAK
                                       | DT_LEFT);
                        textw = mr.right;
                        texth = mr.bottom;
                        s_items[i].wtext = wb;
                    } else {
                        free(wb);
                    }
                }
            }
        }

        if (left && has_think) {
            think_h = s_lineh + CV_LINE_PAD;
            think_w = ui_text_width(hdc, LS(S_THINK_LINK));
        }
        if (nimg > 0) {
            img_h = s_lineh + CV_LINE_PAD;
            img_w = ui_text_width(hdc, LS(S_IMAGE_LINK));
        } else {
            img_h = 0;
        }

        /* stack parts with CV_PART_GAP, trailing gap dropped */
        coth = 0;
        gap = 0;
        if (think_h > 0) {
            coth += think_h;
            gap = CV_PART_GAP;
        }
        if (texth > 0) {
            coth += gap + texth;
            gap = CV_PART_GAP;
        }
        for (j = 0; j < nimg; j++)
            coth += gap + img_h;

        contw = textw;
        if (think_w > contw)
            contw = think_w;
        if (img_w > contw)
            contw = img_w;
        outer_w = contw + 2 * CV_PAD_X;
        if (outer_w < CV_MIN_BUBBLE_W)
            outer_w = CV_MIN_BUBBLE_W;

        body_h = coth + 2 * CV_PAD_Y;
        outer_h = body_h + LLM_BUBBLE_TAIL_H;

        if (left)
            x = CV_MARGIN;
        else
            x = cw - CV_SB_W - CV_MARGIN - outer_w;

        it = &s_items[i];
        it->bubble.left = x;
        it->bubble.top = y;
        it->bubble.right = x + outer_w;
        it->bubble.bottom = y + outer_h;
        it->body = it->bubble;
        it->body.bottom -= LLM_BUBBLE_TAIL_H;
        it->has_think = has_think && left;
        it->textw = textw;
        it->texth = texth;

        /* think link line at the top of the bubble body */
        ty = it->body.top + CV_PAD_Y;
        if (it->has_think) {
            it->think.left = it->body.left + CV_PAD_X;
            it->think.top = ty;
            it->think.right = it->think.left + think_w;
            it->think.bottom = ty + think_h;
            cv_add_link(&it->think, CV_LK_THINK, i, -1);
            ty += think_h + CV_PART_GAP;
        } else {
            it->think.left = it->think.top = it->think.right =
                it->think.bottom = 0;
        }

        /* image link lines, centered, after the text */
        if (texth > 0)
            ty += texth + CV_PART_GAP;
        for (j = 0; j < m->nb; j++) {
            if (m->b[j].kind == BLOCK_IMAGE) {
                RECT lr;
                int lw = img_w;

                if (lw > it->body.right - it->body.left - 2 * CV_PAD_X)
                    lw = it->body.right - it->body.left - 2 * CV_PAD_X;
                lr.left = it->body.left
                    + (it->body.right - it->body.left - lw) / 2;
                lr.top = ty;
                lr.right = lr.left + lw;
                lr.bottom = ty + img_h;
                cv_add_link(&lr, CV_LK_IMAGE, i, j);
                ty += img_h + CV_PART_GAP;
            }
        }

        y += outer_h + CV_GAP;
    }

    s_content_h = s_nitems > 0 ? y - CV_GAP + CV_MARGIN : 0;
    s_layout_valid = 1;

    if (s_pin_bottom)
        s_scroll = cv_max_scroll();
    cv_clamp_scroll();
}

/* ------------------------------------------------------------------ */
/* link hit-testing (Task 18)                                          */

/* Run the layout now if it is stale (hit tests may arrive before the
 * next paint). Needs a DC with ui_font selected, like paint does. */
static void cv_ensure_layout(void)
{
    HDC hdc;
    HGDIOBJ of;

    if (s_layout_valid || s_hwnd == NULL)
        return;
    hdc = GetDC(s_hwnd);
    if (hdc == NULL)
        return;
    of = SelectObject(hdc, ui_font());
    layout_build(hdc);
    SelectObject(hdc, of);
    ReleaseDC(s_hwnd, hdc);
}

/* Content-space point -> link, or NULL. */
static CvLink *cv_link_at(POINT pt_content)
{
    int k;

    for (k = 0; k < s_nlinks; k++)
        if (PtInRect(&s_links[k].rc, pt_content))
            return &s_links[k];
    return NULL;
}

static HCURSOR cv_hand_cursor(void)
{
    static HCURSOR s_hand = NULL;

    if (s_hand == NULL)
        s_hand = LoadCursor(NULL, MAKEINTRESOURCE(32649)); /* IDC_HAND */
    return s_hand;
}

int chatview_hit_link(POINT pt, int *msg_idx, int *kind, int *block_idx)
{
    CvLink *lk;

    cv_ensure_layout();
    pt.y += s_scroll;            /* client -> content space */
    lk = cv_link_at(pt);
    if (lk == NULL)
        return 0;
    if (msg_idx != NULL)
        *msg_idx = lk->msg;
    if (kind != NULL)
        *kind = lk->kind;
    if (block_idx != NULL)
        *block_idx = lk->blk;
    return 1;
}

/* ------------------------------------------------------------------ */
/* painting                                                            */

static void cv_draw_link(HDC m, const RECT *rc, StrID sid)
{
    RECT tr = *rc;
    COLORREF prev;
    int tw;

    prev = SetTextColor(m, LLM_COLOR_LINK);
    ui_drawtext_w(m, LS(sid), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    tw = ui_text_width(m, LS(sid));
    {
        HPEN pen;
        HGDIOBJ old_pen;
        int uy = tr.bottom - 3;

        pen = CreatePen(PS_SOLID, 1, LLM_COLOR_LINK);
        old_pen = SelectObject(m, pen);
        MoveToEx(m, tr.left, uy, NULL);
        LineTo(m, tr.left + tw, uy);
        SelectObject(m, old_pen);
        DeleteObject(pen);
    }
    SetTextColor(m, prev);
}

static void paint_scrollbar(HDC m, int cw, int ch)
{
    RECT sb;
    RECT trk;
    int th, ty;
    HPEN pen;
    HGDIOBJ old;

    sb.left = cw - CV_SB_W;
    sb.top = 0;
    sb.right = cw;
    sb.bottom = ch;
    FillRect(m, &sb, GetSysColorBrush(COLOR_SCROLLBAR));

    th = cv_thumb_h();
    if (th <= 0)
        return;
    ty = cv_thumb_y();

    trk.left = sb.left + 1;
    trk.top = ty;
    trk.right = sb.right - 1;
    trk.bottom = ty + th;
    FillRect(m, &trk, GetSysColorBrush(COLOR_BTNFACE));

    /* raised edge */
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT));
    old = SelectObject(m, pen);
    MoveToEx(m, trk.left, trk.bottom - 1, NULL);
    LineTo(m, trk.left, trk.top);
    LineTo(m, trk.right - 1, trk.top);
    SelectObject(m, old);
    DeleteObject(pen);
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
    old = SelectObject(m, pen);
    MoveToEx(m, trk.right - 1, trk.top, NULL);
    LineTo(m, trk.right - 1, trk.bottom - 1);
    LineTo(m, trk.left - 1, trk.bottom - 1);
    SelectObject(m, old);
    DeleteObject(pen);
}

static void paint_view(HDC m, int cw, int ch)
{
    RECT rc;
    HGDIOBJ old_font;
    COLORREF prev;
    int i, k;

    rc.left = 0;
    rc.top = 0;
    rc.right = cw;
    rc.bottom = ch;
    FillRect(m, &rc, GetSysColorBrush(COLOR_WINDOW));

    /* pre-blended background, pinned to the client bottom-right corner;
     * anchored to the control - s_scroll is deliberately not involved */
    if (s_bg_bmp != NULL && s_bg_w > 0 && s_bg_h > 0)
        BitBlt(m, cw - s_bg_w, ch - s_bg_h, s_bg_w, s_bg_h,
               s_bg_dc, 0, 0, SRCCOPY);

    /* ui_font must be selected before layout_build: measuring and
     * drawing must use the same font or the wrap metrics disagree */
    old_font = SelectObject(m, ui_font());
    SetBkMode(m, TRANSPARENT);
    prev = SetTextColor(m, GetSysColor(COLOR_WINDOWTEXT));

    if (!s_layout_valid)
        layout_build(m);
    if (s_nitems <= 0) {
        SelectObject(m, old_font);
        return;
    }

    for (i = 0; i < s_nitems; i++) {
        Msg *msg = &s_sess->m[i];
        CvItem *it = &s_items[i];
        RECT bub, body, tr;

        bub = it->bubble;
        OffsetRect(&bub, 0, -s_scroll);
        if (bub.bottom < 0 || bub.top > ch)
            continue;

        draw_bubble(m, &bub, msg->role != ROLE_USER,
                    msg->role == ROLE_USER ? LLM_COLOR_USER
                                           : LLM_COLOR_AI);

        body = it->body;
        OffsetRect(&body, 0, -s_scroll);

        if (it->has_think) {
            tr = it->think;
            OffsetRect(&tr, 0, -s_scroll);
            cv_draw_link(m, &tr, S_THINK_LINK);
        }

        if (it->texth > 0 && it->wtext != NULL) {
            RECT txt;

            txt.left = body.left + CV_PAD_X;
            txt.top = body.top + CV_PAD_Y
                + (it->has_think
                   ? (it->think.bottom - it->think.top) + CV_PART_GAP
                   : 0);
            txt.right = txt.left + it->textw;
            txt.bottom = txt.top + it->texth;
            ui_drawtext_ww(m, it->wtext, &txt, DT_LEFT | DT_WORDBREAK);
        }

        for (k = 0; k < s_nlinks; k++) {
            if (s_links[k].kind == CV_LK_IMAGE
                && s_links[k].msg == i) {
                tr = s_links[k].rc;
                OffsetRect(&tr, 0, -s_scroll);
                cv_draw_link(m, &tr, S_IMAGE_LINK);
            }
        }
    }

    SetTextColor(m, prev);
    SelectObject(m, old_font);
}

static void paint(HDC hdc, const RECT *client)
{
    int cw = client->right;
    int ch = client->bottom;

    if (cw < 1 || ch < 1)
        return;

    if (s_memdc == NULL) {
        s_memdc = CreateCompatibleDC(hdc);
        if (s_memdc == NULL)
            return;
    }
    if (s_bmp == NULL || s_bw != cw || s_bh != ch) {
        HBITMAP nb = CreateCompatibleBitmap(hdc, cw, ch);
        if (nb == NULL)
            return;
        if (s_bmp != NULL) {
            SelectObject(s_memdc, s_oldbmp);
            DeleteObject(s_bmp);
        }
        s_bmp = nb;
        s_oldbmp = SelectObject(s_memdc, s_bmp);
        s_bw = cw;
        s_bh = ch;
    }

    paint_view(s_memdc, cw, ch);
    paint_scrollbar(s_memdc, cw, ch);
    BitBlt(hdc, 0, 0, cw, ch, s_memdc, 0, 0, SRCCOPY);
}

/* ------------------------------------------------------------------ */
/* window proc                                                         */

static LRESULT CALLBACK chatview_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp)
{
    PAINTSTRUCT ps;
    RECT rc;
    int cw, ch, x, y;

    switch (msg) {
    case WM_CREATE:
        s_hwnd = hwnd;
        return 0;

    case WM_SIZE:
        s_layout_valid = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        paint(ps.hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_MOUSEWHEEL:
        if (cv_thumb_h() > 0) {
            int delta = (int)(short)HIWORD(wp);
            cv_scroll_by(-delta * (CV_WHEEL_LINES * (s_lineh + CV_GAP))
                         / 120);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            CvLink *lk;

            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            cv_ensure_layout();
            pt.y += s_scroll;
            lk = cv_link_at(pt);
            if (lk != NULL && cv_hand_cursor() != NULL) {
                SetCursor(cv_hand_cursor());
                return TRUE;
            }
        }
        return DefWindowProc(hwnd, msg, wp, lp);

    case WM_LBUTTONDOWN:
        cw = cv_view_w();
        ch = cv_view_h();
        x = (int)(short)LOWORD(lp);
        y = (int)(short)HIWORD(lp);
        if (x >= cw - CV_SB_W && cv_thumb_h() > 0) {
            int ty = cv_thumb_y();
            int th = cv_thumb_h();

            if (y >= ty && y < ty + th) {
                s_drag = 1;
                s_drag_off = y - ty;
                SetCapture(hwnd);
            } else {
                cv_scroll_by(y < ty ? -ch : ch);
            }
            return 0;
        }
        {
            POINT pt;
            CvLink *lk;

            cv_ensure_layout();
            pt.x = x;
            pt.y = y + s_scroll;
            lk = cv_link_at(pt);
            if (lk != NULL) {
                /* notify mainwin AFTER we return from this handler */
                if (lk->kind == CV_LK_THINK)
                    PostMessage(g_hwnd_main, WM_APP_OPEN_THINK,
                                (WPARAM)lk->msg, 0);
                else
                    PostMessage(g_hwnd_main, WM_APP_OPEN_IMAGE,
                                (WPARAM)lk->msg, (LPARAM)lk->blk);
                return 0;
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (s_drag) {
            cw = cv_view_w();
            ch = cv_view_h();
            y = (int)(short)HIWORD(lp);
            {
                int th = cv_thumb_h();
                int ms = cv_max_scroll();
                int ny = y - s_drag_off;

                if (ny < 0)
                    ny = 0;
                if (ny > ch - th)
                    ny = ch - th;
                if (ms > 0 && ch > th)
                    cv_scroll_to((ny * ms) / (ch - th));
            }
            return 0;
        }
        return 0;

    case WM_LBUTTONUP:
        if (s_drag) {
            s_drag = 0;
            ReleaseCapture();
            return 0;
        }
        return 0;

    case WM_DESTROY:
        layout_free();
        cv_bg_free();
        free(s_tbuf);
        s_tbuf = NULL;
        s_tcap = 0;
        if (s_memdc != NULL) {
            if (s_bmp != NULL) {
                SelectObject(s_memdc, s_oldbmp);
                DeleteObject(s_bmp);
                s_bmp = NULL;
            }
            DeleteDC(s_memdc);
            s_memdc = NULL;
        }
        s_oldbmp = NULL;
        s_bw = s_bh = 0;
        s_hwnd = NULL;
        s_sess = NULL;
        s_scroll = 0;
        s_pin_bottom = 1;
        s_layout_valid = 0;
        s_drag = 0;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* public API                                                          */

HWND chatview_create(HWND parent)
{
    HINSTANCE inst;
    WNDCLASSA wc;

    if (parent == NULL)
        return NULL;
    inst = GetModuleHandle(NULL);
    if (!s_registered) {
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = chatview_wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = CV_CLASS;
        if (!RegisterClassA(&wc))
            return NULL;
        s_registered = 1;
    }
    return CreateWindowExA(0, CV_CLASS, NULL,
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           0, 0, 0, 0, parent, NULL, inst, NULL);
}

void chatview_set_session(Session *s)
{
    s_sess = s;
    s_scroll = 0;
    s_pin_bottom = 1;
    s_layout_valid = 0;
    if (s_hwnd != NULL)
        InvalidateRect(s_hwnd, NULL, TRUE);
}

void chatview_invalidate_msg(int msg_idx)
{
    (void)msg_idx;   /* full relayout for now, see chatview.h */
    s_layout_valid = 0;
    if (s_hwnd != NULL)
        InvalidateRect(s_hwnd, NULL, FALSE);
}

void chatview_scroll_bottom(void)
{
    s_pin_bottom = 1;
    s_layout_valid = 0;
    if (s_hwnd != NULL)
        InvalidateRect(s_hwnd, NULL, FALSE);
}

void chatview_set_background(const char *path_utf8, int opacity)
{
    /* Always rebuild from scratch: the old bitmap is freed first so an
     * opacity/path change cannot leak or keep stale pixels. Empty path,
     * opacity <= 0 (the blended result would be pure white anyway) or a
     * decode failure all leave s_bg_bmp NULL = plain white background.
     * Load failures are silent by choice (YAGNI: no status-strip note);
     * opacity is clamped inside draw_blend_white, and cv_bg_build
     * re-clamps implicitly via the same helper. */
    cv_bg_free();
    if (path_utf8 != NULL && path_utf8[0] != '\0' && opacity > 0)
        (void)cv_bg_build(path_utf8, opacity);
    if (s_hwnd != NULL)
        InvalidateRect(s_hwnd, NULL, TRUE);
}
