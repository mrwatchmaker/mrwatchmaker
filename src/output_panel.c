/*
    tg
    Copyright (C) 2015 Marcello Mamino

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "tg.h"
#include "i18n.h"
#include "serial_motor.h"
#ifdef _WIN32
#include <windows.h>
#endif

// ── Watch Winder 프리셋 (init_output_panel에서 사용하므로 파일 최상단) ──
typedef struct { const char *name; const int *seq; int len; } WinderPreset;

static const int wp0[] = {3,0,1,2,3,2,1,0};
static const int wp1[] = {0,1,2,3};
static const int wp2[] = {0,3,2,1};
static const int wp3[] = {0,2};
static const int wp4[] = {1,3};
static const int wp5[] = {0,3,0,3};
static const int wp6[] = {3,0,1,0};
static const int wp7[] = {0,1,2,3,2,1};

static const int wp_pos[] = {0,1,2,3,4,5,6,7}; /* 자세차 순서 프리셋용 (0..3 고정, 4+ 커스텀) */
static const int wp_custom[] = {0,1,2,3};      /* 커스텀만 반복용 (실제 길이는 num_custom) */

static const WinderPreset winder_presets[] = {
	{"풀 코스 (6→9→12→3→6→3→12→9)",     wp0, 8},
	{"시계방향 회전 (9→12→3→6)",          wp1, 4},
	{"반시계방향 회전 (9→6→3→12)",        wp2, 4},
	{"좌우 스윙 (9시 ↔ 3시)",             wp3, 2},
	{"상하 스윙 (12시 ↔ 6시)",            wp4, 2},
	{"가볍게 흔들기 (9시 ↔ 6시)",         wp5, 4},
	{"6→9→12→9 왕복",                    wp6, 4},
	{"시계방향 왕복 (9→12→3→6→3→12→9)", wp7, 6},
	{"자세차 자동측정 순서 (반복)",       wp_pos, 8},   /* 실제 길이는 4+num_custom */
	{"커스텀 1~N 반복",                  wp_custom, 4}, /* 실제 길이는 num_custom */
};
#define WINDER_PRESET_COUNT 10
#define WINDER_PRESET_POSITIONAL 8
#define WINDER_PRESET_CUSTOM_ONLY 9

/* ── STS3215 소프트웨어 안전 범위 (사용자 지정) ───────────────────── */
#define FACE_MIN 1935
#define FACE_MAX 4087
#define ARM_MIN  125
#define ARM_MAX  2071

/* 끝단 근처(하드스톱/기구 간섭/토크 부족)에서 버징/떨림이 생기기 쉬워
 * "와인더 동작"에 한해서만 여유를 두고 안쪽으로 클램프한다.
 * (수동 이동/자세차 측정은 사용자가 지정한 목표값까지 정확히 이동해야 하므로 제외) */
/* 와인더에서 끝단 보정이 과하면 "끝까지 안 가는" 문제가 생김.
 * 와인더는 정지 없이 계속 움직이게(홀드 시간 0) 만들 것이므로,
 * 끝단 보정은 제거하고 하드 안전범위만 적용한다. */
#define FACE_SOFT_MARGIN 0
#define ARM_SOFT_MARGIN  0

/* 와인더는 '밥 주기' 용이라 멈춰서 자세를 재는 개념이 아님.
 * 한 자세에서 오래 머무르지 않고, 부드럽게 계속 돌도록 한다. */
#define WINDER_SETTLE_ENABLE 0

/* 와인더(유튜브 링크 추가 직전 상태): 1초마다 다음 자세로 이동(대기 없음) */
#define WINDER_TICK_SEC 1

/* 와인더 전용 이동 시간(짧게/과격하게) */
#define WINDER_MIN_MS 2500
#define WINDER_MAX_MS 6000
#define WINDER_SPEED_LIMIT 0

cairo_pattern_t *black,*white,*red,*green,*blue,*blueish,*yellow;
// MrWatchmaker 전용 색상
static cairo_pattern_t *navy;   // 배경 (#12121e)
static cairo_pattern_t *gold;   // 정상 (#c8a840)
static cairo_pattern_t *dimgold;// 비활성 바 (#3a3018)
static cairo_pattern_t *silver; // 레이블 (#8090aa)
static cairo_pattern_t *bright; // 숫자 (#e8f0ff)

static void define_color(cairo_pattern_t **gc,double r,double g,double b)
{
	*gc = cairo_pattern_create_rgb(r,g,b);
}

void initialize_palette()
{
	define_color(&black,   0,     0,     0    );
	define_color(&white,   1,     1,     1    );
	define_color(&red,     0.85,  0.15,  0.15 );
	define_color(&green,   0,     0.8,   0    );
	define_color(&blue,    0,     0,     1    );
	define_color(&blueish, 0,     0,     0.5  );
	define_color(&yellow,  1,     1,     0    );
	// MrWatchmaker 팔레트
	define_color(&navy,    0.071, 0.071, 0.118); // #12121e
	define_color(&gold,    0.784, 0.659, 0.251); // #c8a840
	define_color(&dimgold, 0.18,  0.14,  0.06 ); // 비활성 바
	define_color(&silver,  0.50,  0.56,  0.67 ); // #8090aa
	define_color(&bright,  0.91,  0.94,  1.00 ); // #e8f0ff
}

static void draw_graph(double a, double b, cairo_t *c, struct processing_buffers *p, GtkWidget *da)
{
	GtkAllocation temp;
	gtk_widget_get_allocation (da, &temp);
	int width = temp.width;
	int height = temp.height;

	int n;

	int first = 1;
	for(n=0; n<2*width; n++) {
		int i = n < width ? n : 2*width - 1 - n;
		double x = fmod(a + i * (b-a) / width, p->period);
		if(x < 0) x += p->period;
		int j = floor(x);
		double y;

		if(p->waveform[j] <= 0) y = 0;
		else y = p->waveform[j] * 0.4 / p->waveform_max;

		int k = round(y*height);
		if(n < width) k = -k;

		if(first) {
			cairo_move_to(c,i+.5,height/2+k+.5);
			first = 0;
		} else
			cairo_line_to(c,i+.5,height/2+k+.5);
	}
}

#ifdef DEBUG
static void draw_debug_graph(double a, double b, cairo_t *c, struct processing_buffers *p, GtkWidget *da)
{
	if(!p->debug) return;

	GtkAllocation temp;
	gtk_widget_get_allocation (da, &temp);
	int width = temp.width;
	int height = temp.height;

	int i;
	float max = 0;

	int ai = round(a);
	int bi = 1+round(b);
	if(ai < 0) ai = 0;
	if(bi > p->sample_count) bi = p->sample_count;
	for(i=ai; i<bi; i++)
		if(p->debug[i] > max)
			max = p->debug[i];

	int first = 1;
	for(i=0; i<width; i++) {
		if( round(a + i*(b-a)/width) != round(a + (i+1)*(b-a)/width) ) {
			int j = round(a + i*(b-a)/width);
			if(j < 0) j = 0;
			if(j >= p->sample_count) j = p->sample_count-1;

			int k = round((0.1+p->debug[j]/max)*0.8*height);

			if(first) {
				cairo_move_to(c,i+.5,height-k-.5);
				first = 0;
			} else
				cairo_line_to(c,i+.5,height-k-.5);
		}
	}
}
#endif

static double amplitude_to_time(double lift_angle, double amp)
{
	return asin(lift_angle / (2 * amp)) / M_PI;
}

// ── MrWatchmaker 기어 아이콘 ────────────────────────────────────────
static double draw_watch_icon(cairo_t *c, int signal, int happy, int light, double H)
{
	UNUSED(light);
	happy = !!happy;
	cairo_pattern_t *col = happy ? gold : red;

	double cx = H * 0.50;
	double cy = H * 0.50;
	double Ro = H * 0.40; // 기어 이빨 바깥 반지름
	double Ri = H * 0.28; // 기어 이빨 안쪽 반지름
	double Rc = H * 0.10; // 가운데 원
	int    N  = 8;        // 이빨 수

	// ── 기어 외곽 (이빨 8개) ────────────────────────────────────────
	cairo_set_line_width(c, 2.0);
	cairo_set_source(c, col);
	cairo_new_path(c);
	for (int i = 0; i < N * 4; i++) {
		double a = 2.0 * M_PI * i / (N * 4);
		double r = (i % 4 < 2) ? Ro : Ri;
		if (i == 0) cairo_move_to(c, cx + r * cos(a), cy + r * sin(a));
		else        cairo_line_to(c, cx + r * cos(a), cy + r * sin(a));
	}
	cairo_close_path(c);
	cairo_stroke(c);

	// ── 내부 링 ─────────────────────────────────────────────────────
	cairo_set_line_width(c, 1.2);
	cairo_arc(c, cx, cy, Ri * 0.78, 0, 2 * M_PI);
	cairo_stroke(c);

	// ── 중심 채우기 ─────────────────────────────────────────────────
	cairo_arc(c, cx, cy, Rc, 0, 2 * M_PI);
	cairo_fill(c);

	// ── 신호 강도: 높이가 다른 수직 바 ─────────────────────────────
	double bar_w  = H * 0.06;
	double gap    = H * 0.025;
	double base_y = H * 0.88;
	double max_h  = H * 0.72;
	double start_x = H + gap;

	for (int i = 0; i < NSTEPS; i++) {
		double bh = max_h * (i + 1.0) / NSTEPS;
		double bx = start_x + i * (bar_w + gap);
		cairo_set_source(c, i < signal ? col : dimgold);
		cairo_rectangle(c, bx, base_y - bh, bar_w, bh);
		cairo_fill(c);
	}

	return start_x + NSTEPS * (bar_w + gap) + gap * 2;
}

static void cairo_init(cairo_t *c)
{
	cairo_set_line_width(c,1);
	cairo_set_source(c, navy); // 앱 배경색과 통일
	cairo_paint(c);
}

static double print_s(cairo_t *c, double x, double y, char *s)
{
	cairo_text_extents_t extents;
	cairo_move_to(c,x,y);
	cairo_show_text(c,s);
	cairo_text_extents(c,s,&extents);
	x += extents.x_advance;
	return x;
}

static double print_number(cairo_t *c, double x, double y, char *s)
{
	cairo_text_extents_t extents;
	cairo_text_extents(c,"0",&extents);
	double z = extents.x_advance;
	char t[2];
	t[1] = 0;
	while((t[0] = *s++)) {
		cairo_text_extents(c,t,&extents);
		cairo_move_to(c, x + (z - extents.x_advance) / 2, y);
		cairo_show_text(c,t);
		x += z;
	}
	return x;
}

static gboolean output_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	cairo_init(c);

	// 위젯 실제 크기에 맞춰 폰트와 아이콘 자동 스케일 (아이콘은 정사각형 유지로 찌그러짐 방지)
	GtkAllocation alloc;
	gtk_widget_get_allocation(widget, &alloc);
	double W = (double)alloc.width;
	double H = (double)alloc.height;
	double icon_box = W < H ? W : H;
	if (icon_box < 80) icon_box = 80;
	if (icon_box > 220) icon_box = 220;
	double H_font = H > 80 ? H : 80.0;
	double font = H_font * 0.45;
	if (font < 40)  font = 40;
	if (font > 220) font = 220;

	struct snapshot *snst = op->snst;
	struct processing_buffers *p = snst->pb;
	int old = snst->is_old;

	// 아이콘을 1:1 비율로 그리기 (cairo 스케일로 정사각형 강제)
	cairo_save(c);
	cairo_scale(c, icon_box / 80.0, icon_box / 80.0);
	double x0 = draw_watch_icon(c, snst->signal,
	                             snst->calibrate ? snst->signal==NSTEPS : snst->signal,
	                             snst->is_light, 80.0);
	cairo_restore(c);
	double x = x0 * (icon_box / 80.0);

	cairo_text_extents_t extents;

	cairo_set_font_size(c, font);
	cairo_text_extents(c,"0",&extents);
	double y = H_font/2.0 - extents.y_bearing - extents.height/2.0;

	if(snst->calibrate) {
		cairo_set_source(c, white);
		x = print_s(c,x,y,"cal");
		cairo_set_font_size(c, font*2/3);
		x = print_s(c,x,y," (");
		cairo_move_to(c,x,y);
		{
			double a = 0;
			char *s[] = {"wait", "acq.", "done", "fail", NULL}, **t = s;
			for(;*t;t++) {
				cairo_text_extents(c,*t,&extents);
				if(a < extents.x_advance) a = extents.x_advance;
			}
			x += a;
		}
		switch(snst->cal_state) {
			case 1:
				cairo_set_source(c,green);
				cairo_show_text(c,"done");
				break;
			case 0:
				cairo_set_source(c, snst->signal == NSTEPS ? white : yellow);
				cairo_show_text(c, snst->signal == NSTEPS ? "acq." : "wait");
				break;
			case -1:
				cairo_set_source(c,red);
				cairo_show_text(c,"fail");
				break;
		}
		cairo_set_source(c, white);
		x = print_s(c,x,y,")");
		cairo_set_font_size(c, font);
		char s[20];
		switch(snst->cal_state) {
			case 1:
				sprintf(s, " %s%d.%d",
						snst->cal_result < 0 ? "-" : "+",
						abs(snst->cal_result) / 10,
						abs(snst->cal_result) % 10 );
				x = print_s(c,x,y,s);
				cairo_set_font_size(c, font*2/3);
				x = print_s(c,x,y," s/d");
				break;
			case 0:
				sprintf(s, " %d", snst->cal_percent);
				x = print_number(c,x,y,s);
				x = print_s(c,x,y," %");
				break;
		}
	} else {
		char outputs[8][20];
		double tic_amp = 0, toc_amp = 0;
		if(p) {
			int rate = round(snst->rate);
			double be = snst->be;
			char rates[20];
			sprintf(rates,"%s%d",rate > 0 ? "+" : rate < 0 ? "-" : "",abs(rate));
			sprintf(outputs[0],"%4s",rates);
			sprintf(outputs[2]," %4.1f",be);
			if(snst->amp > 0) {
				sprintf(outputs[4]," %3.0f",snst->amp);
				// 좌/우 진폭 계산 (tic=Left, toc=Right)
				if(p->period > 0 && snst->la > 0) {
					double tp = p->tic_pulse, tocp = p->toc_pulse;
					double la = snst->la, per = p->period;
					double sin_t = sin(M_PI * tp   / per);
					double sin_T = sin(M_PI * tocp / per);
					if(sin_t > 0.01 && sin_T > 0.01) {
						tic_amp = la * 0.5 / sin_t;
						toc_amp = la * 0.5 / sin_T;
						// 유효 범위 확인
						if(tic_amp < 100 || tic_amp > 400) tic_amp = 0;
						if(toc_amp < 100 || toc_amp > 400) toc_amp = 0;
					}
				}
			} else {
				strcpy(outputs[4]," ---");
			}
		} else {
			strcpy(outputs[0],"----");
			strcpy(outputs[2]," ----");
			strcpy(outputs[4]," ---");
		}
		sprintf(outputs[6]," %d",snst->guessed_bph);

		strcpy(outputs[1]," s/d");
		strcpy(outputs[3]," ms");
		strcpy(outputs[5]," deg");
		strcpy(outputs[7]," bph");

		// ── 메인 라인: 4개 값 ──────────────────────────────────────────
		// deg 값이 그려지는 x 위치를 기억하기 위해 구간 추적
		double x_deg_start = 0, x_deg_end = 0;
		int i;
		for(i=0; i<8; i++) {
			if(i == 4) x_deg_start = x;  // amp 숫자 시작점
			if(i%2) {
				cairo_set_source(c, silver);
				cairo_set_font_size(c, font*2/3);
				x = print_s(c,x,y,outputs[i]);
				if(i == 5) x_deg_end = x;  // " deg" 끝점
			} else {
				cairo_set_source(c, i > 4 || !p || !old ? bright : gold);
				cairo_set_font_size(c, font);
				x = print_number(c,x,y,outputs[i]);
			}
		}

		// ── 서브 라인: deg 아래에 (L: xxx°  R: xxx°) ─────────────────
		if(tic_amp > 0 && toc_amp > 0) {
			char lr_buf[48];
			snprintf(lr_buf, sizeof(lr_buf),
			         "  (L: %.0f°   R: %.0f°)", tic_amp, toc_amp);
			double sub_font = font * 0.38;
			double sub_y    = y + font * 0.62;
			cairo_set_font_size(c, sub_font);
			cairo_set_source(c, silver);
			cairo_move_to(c, x_deg_start, sub_y);
			cairo_show_text(c, lr_buf);
		}
	}
#ifdef DEBUG
	{
		static GTimer *timer = NULL;
		if (!timer) timer = g_timer_new();
		else {
			char s[100];
			sprintf(s,"  %.2f fps",1./g_timer_elapsed(timer, NULL));
			cairo_set_source(c, white);
			cairo_set_font_size(c, font);
			x = print_s(c,x,y,s);
			g_timer_reset(timer);
		}
	}
#endif

	return FALSE;
}

static void expose_waveform(
			struct output_panel *op,
			GtkWidget *da,
			cairo_t *c,
			int (*get_offset)(struct processing_buffers*),
			double (*get_pulse)(struct processing_buffers*))
{
	cairo_init(c);

	GtkAllocation temp;
	gtk_widget_get_allocation(da, &temp);

	int width = temp.width;
	int height = temp.height;

	gtk_widget_get_allocation(gtk_widget_get_toplevel(da), &temp);
	int font = temp.width / 90;
	if(font < 12)
		font = 12;
	int i;

	cairo_set_font_size(c,font);

	for(i = 1-NEGATIVE_SPAN; i < POSITIVE_SPAN; i++) {
		int x = (NEGATIVE_SPAN + i) * width / (POSITIVE_SPAN + NEGATIVE_SPAN);
		cairo_move_to(c, x + .5, height / 2 + .5);
		cairo_line_to(c, x + .5, height - .5);
		if(i%5)
			cairo_set_source(c,green);
		else
			cairo_set_source(c,red);
		cairo_stroke(c);
	}
	cairo_set_source(c,white);
	for(i = 1-NEGATIVE_SPAN; i < POSITIVE_SPAN; i++) {
		if(!(i%5)) {
			int x = (NEGATIVE_SPAN + i) * width / (POSITIVE_SPAN + NEGATIVE_SPAN);
			char s[10];
			sprintf(s,"%d",i);
			cairo_move_to(c,x+font/4,height-font/2);
			cairo_show_text(c,s);
		}
	}

	cairo_text_extents_t extents;

	cairo_text_extents(c,"ms",&extents);
	cairo_move_to(c,width - extents.x_advance - font/4,height-font/2);
	cairo_show_text(c,"ms");

	struct snapshot *snst = op->snst;
	struct processing_buffers *p = snst->pb;
	int old = snst->is_old;
	double period = p ? p->period / snst->sample_rate : 7200. / snst->guessed_bph;

	for(i = 10; i < 360; i+=10) {
		if(2*i < snst->la) continue;
		double t = period*amplitude_to_time(snst->la,i);
		if(t > .001 * NEGATIVE_SPAN) continue;
		int x = round(width * (NEGATIVE_SPAN - 1000*t) / (NEGATIVE_SPAN + POSITIVE_SPAN));
		cairo_move_to(c, x+.5, .5);
		cairo_line_to(c, x+.5, height / 2 + .5);
		if(i % 50)
			cairo_set_source(c,green);
		else
			cairo_set_source(c,red);
		cairo_stroke(c);
	}

	double last_x = 0;
	cairo_set_source(c,white);
	for(i = 50; i < 360; i+=50) {
		double t = period*amplitude_to_time(snst->la,i);
		if(t > .001 * NEGATIVE_SPAN) continue;
		int x = round(width * (NEGATIVE_SPAN - 1000*t) / (NEGATIVE_SPAN + POSITIVE_SPAN));
		if(x > last_x) {
			char s[10];

			sprintf(s,"%d",abs(i));
			cairo_move_to(c, x + font/4, font * 3 / 2);
			cairo_show_text(c,s);
			cairo_text_extents(c,s,&extents);
			last_x = x + font/4 + extents.x_advance;
		}
	}

	cairo_text_extents(c,"deg",&extents);
	cairo_move_to(c,width - extents.x_advance - font/4,font * 3 / 2);
	cairo_show_text(c,"deg");

	if(p) {
		double span = 0.001 * snst->sample_rate;
		int offset = get_offset(p);

		double a = offset - span * NEGATIVE_SPAN;
		double b = offset + span * POSITIVE_SPAN;

		draw_graph(a,b,c,p,da);

		cairo_set_source(c,old?yellow:white);
		cairo_stroke_preserve(c);
		cairo_fill(c);

		double pulse = get_pulse(p);
		if(pulse > 0) {
			int x = round((NEGATIVE_SPAN - pulse / span) * width / (POSITIVE_SPAN + NEGATIVE_SPAN));
			cairo_move_to(c, x, 1);
			cairo_line_to(c, x, height - 1);
			cairo_set_source(c,blue);
			cairo_set_line_width(c,2);
			cairo_stroke(c);
		}
	} else {
		cairo_move_to(c, .5, height / 2 + .5);
		cairo_line_to(c, width - .5, height / 2 + .5);
		cairo_set_source(c,yellow);
		cairo_stroke(c);
	}
}

static int get_tic(struct processing_buffers *p)
{
	return p->tic;
}

static int get_toc(struct processing_buffers *p)
{
	return p->toc;
}

static double get_tic_pulse(struct processing_buffers *p)
{
	return p->tic_pulse;
}

static double get_toc_pulse(struct processing_buffers *p)
{
	return p->toc_pulse;
}

static gboolean tic_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	expose_waveform(op, op->tic_drawing_area, c, get_tic, get_tic_pulse);
	return FALSE;
}

static gboolean toc_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	expose_waveform(op, op->toc_drawing_area, c, get_toc, get_toc_pulse);
	return FALSE;
}

static gboolean period_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	cairo_init(c);

	GtkAllocation temp;
	gtk_widget_get_allocation (op->period_drawing_area, &temp);

	int width = temp.width;
	int height = temp.height;

	struct snapshot *snst = op->snst;
	struct processing_buffers *p = snst->pb;
	int old = snst->is_old;

	double toc,a=0,b=0;

	if(p) {
		toc = p->tic < p->toc ? p->toc : p->toc + p->period;
		a = ((double)p->tic + toc)/2 - p->period/2;
		b = ((double)p->tic + toc)/2 + p->period/2;

		cairo_move_to(c, (p->tic - a - NEGATIVE_SPAN*.001*snst->sample_rate) * width/p->period, 0);
		cairo_line_to(c, (p->tic - a - NEGATIVE_SPAN*.001*snst->sample_rate) * width/p->period, height);
		cairo_line_to(c, (p->tic - a + POSITIVE_SPAN*.001*snst->sample_rate) * width/p->period, height);
		cairo_line_to(c, (p->tic - a + POSITIVE_SPAN*.001*snst->sample_rate) * width/p->period, 0);
		cairo_set_source(c,blueish);
		cairo_fill(c);

		cairo_move_to(c, (toc - a - NEGATIVE_SPAN*.001*snst->sample_rate) * width/p->period, 0);
		cairo_line_to(c, (toc - a - NEGATIVE_SPAN*.001*snst->sample_rate) * width/p->period, height);
		cairo_line_to(c, (toc - a + POSITIVE_SPAN*.001*snst->sample_rate) * width/p->period, height);
		cairo_line_to(c, (toc - a + POSITIVE_SPAN*.001*snst->sample_rate) * width/p->period, 0);
		cairo_set_source(c,blueish);
		cairo_fill(c);
	}

	int i;
	for(i = 1; i < 16; i++) {
		int x = i * width / 16;
		cairo_move_to(c, x+.5, .5);
		cairo_line_to(c, x+.5, height - .5);
		if(i % 4)
			cairo_set_source(c,green);
		else
			cairo_set_source(c,red);
		cairo_stroke(c);
	}

	if(p) {
		draw_graph(a,b,c,p,op->period_drawing_area);

		cairo_set_source(c,old?yellow:white);
		cairo_stroke_preserve(c);
		cairo_fill(c);
	} else {
		cairo_move_to(c, .5, height / 2 + .5);
		cairo_line_to(c, width - .5, height / 2 + .5);
		cairo_set_source(c,yellow);
		cairo_stroke(c);
	}

	return FALSE;
}

static gboolean paperstrip_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	int i;
	struct snapshot *snst = op->snst;
	uint64_t time = snst->timestamp ? snst->timestamp : get_timestamp(snst->is_light);
	double sweep;
	int zoom_factor;
	double slope = 1000; // detected rate: 1000 -> do not display
	if(snst->calibrate) {
		sweep = snst->nominal_sr;
		zoom_factor = PAPERSTRIP_ZOOM_CAL;
		slope = (double) snst->cal * zoom_factor / (10 * 3600 * 24);
	} else {
		sweep = snst->sample_rate * 3600. / snst->guessed_bph;
		zoom_factor = PAPERSTRIP_ZOOM;
		if(snst->events_count && snst->events[snst->events_wp])
			slope = - snst->rate * zoom_factor / (3600. * 24.);
	}

	cairo_init(c);

	GtkAllocation temp;
	gtk_widget_get_allocation (op->paperstrip_drawing_area, &temp);

	int width = temp.width;
	int height = temp.height;

	int stopped = 0;
	if( snst->events_count &&
	    snst->events[snst->events_wp] &&
	    time > 5 * snst->nominal_sr + snst->events[snst->events_wp]) {
		time = 5 * snst->nominal_sr + snst->events[snst->events_wp];
		stopped = 1;
	}

	int strip_width = round(width / (1 + PAPERSTRIP_MARGIN));

	cairo_set_line_width(c,1.3);

	slope *= strip_width;
	if(slope <= 2 && slope >= -2) {
		for(i=0; i<4; i++) {
			double y = 0;
			cairo_move_to(c, (double)width * (i+.5) / 4, 0);
			for(;;) {
				double x = y * slope + (double)width * (i+.5) / 4;
				x = fmod(x, width);
				if(x < 0) x += width;
				double nx = x + slope * (height - y);
				if(nx >= 0 && nx <= width) {
					cairo_line_to(c, nx, height);
					break;
				} else {
					double d = slope > 0 ? width - x : x;
					y += d / fabs(slope);
					cairo_line_to(c, slope > 0 ? width : 0, y);
					y += 1;
					if(y > height) break;
					cairo_move_to(c, slope > 0 ? 0 : width, y);
				}
			}
		}
		cairo_set_source(c, blue);
		cairo_stroke(c);
	}

	cairo_set_line_width(c,1);

	int left_margin = (width - strip_width) / 2;
	int right_margin = (width + strip_width) / 2;
	cairo_move_to(c, left_margin + .5, .5);
	cairo_line_to(c, left_margin + .5, height - .5);
	cairo_move_to(c, right_margin + .5, .5);
	cairo_line_to(c, right_margin + .5, height - .5);
	cairo_set_source(c, green);
	cairo_stroke(c);

	double now = sweep*ceil(time/sweep);
	double ten_s = snst->sample_rate * 10 / sweep;
	double last_line = fmod(now/sweep, ten_s);
	int last_tenth = floor(now/(sweep*ten_s));
	for(i=0;;i++) {
		double y = 0.5 + round(last_line + i*ten_s);
		if(y > height) break;
		cairo_move_to(c, .5, y);
		cairo_line_to(c, width-.5, y);
		cairo_set_source(c, (last_tenth-i)%6 ? green : red);
		cairo_stroke(c);
	}

	cairo_set_source(c,stopped?yellow:white);
	for(i = snst->events_wp;;) {
		if(!snst->events_count || !snst->events[i]) break;
		double event = now - snst->events[i] + snst->trace_centering + sweep * PAPERSTRIP_MARGIN / (2 * zoom_factor);
		int column = floor(fmod(event, (sweep / zoom_factor)) * strip_width / (sweep / zoom_factor));
		int row = floor(event / sweep);
		if(row >= height) break;
		cairo_move_to(c,column,row);
		cairo_line_to(c,column+1,row);
		cairo_line_to(c,column+1,row+1);
		cairo_line_to(c,column,row+1);
		cairo_line_to(c,column,row);
		cairo_fill(c);
		if(column < width - strip_width && row > 0) {
			column += strip_width;
			row -= 1;
			cairo_move_to(c,column,row);
			cairo_line_to(c,column+1,row);
			cairo_line_to(c,column+1,row+1);
			cairo_line_to(c,column,row+1);
			cairo_line_to(c,column,row);
			cairo_fill(c);
		}
		if(--i < 0) i = snst->events_count - 1;
		if(i == snst->events_wp) break;
	}

	cairo_set_source(c,white);
	cairo_set_line_width(c,2);
	cairo_move_to(c, left_margin + 3, height - 20.5);
	cairo_line_to(c, right_margin - 3, height - 20.5);
	cairo_stroke(c);
	cairo_set_line_width(c,1);
	cairo_move_to(c, left_margin + .5, height - 20.5);
	cairo_line_to(c, left_margin + 5.5, height - 15.5);
	cairo_line_to(c, left_margin + 5.5, height - 25.5);
	cairo_line_to(c, left_margin + .5, height - 20.5);
	cairo_fill(c);
	cairo_move_to(c, right_margin + .5, height - 20.5);
	cairo_line_to(c, right_margin - 4.5, height - 15.5);
	cairo_line_to(c, right_margin - 4.5, height - 25.5);
	cairo_line_to(c, right_margin + .5, height - 20.5);
	cairo_fill(c);

	char s[100];
	cairo_text_extents_t extents;

	gtk_widget_get_allocation(gtk_widget_get_toplevel(widget), &temp);
	int font = temp.width / 90;
	if(font < 12)
		font = 12;
	cairo_set_font_size(c,font);

	sprintf(s, "%.1f ms", snst->calibrate ?
				1000. / zoom_factor :
				3600000. / (snst->guessed_bph * zoom_factor));
	cairo_text_extents(c,s,&extents);
	cairo_move_to(c, (width - extents.x_advance)/2, height - 30);
	cairo_show_text(c,s);

	return FALSE;
}

#ifdef DEBUG
static gboolean debug_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	cairo_init(c);

	struct snapshot *snst = op->snst;
	struct processing_buffers *p;
	if(snst->calibrate)
		p = &op->computer->pdata->buffers[0];
	else
		p = snst->pb;

	if(p) {
		double a = snst->nominal_sr / 10;
		double b = snst->nominal_sr * 2;

		draw_debug_graph(a,b,c,p,op->debug_drawing_area);

		cairo_set_source(c,snst->is_old?yellow:white);
		cairo_stroke(c);
	}

	return FALSE;
}
#endif

static void handle_clear_trace(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	if(op->computer) {
		lock_computer(op->computer);
		if(!op->snst->calibrate) {
			memset(op->snst->events,0,op->snst->events_count*sizeof(uint64_t));
			op->computer->clear_trace = 1;
		}
		unlock_computer(op->computer);
		gtk_widget_queue_draw(op->paperstrip_drawing_area);
	}
}

static void handle_center_trace(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	struct snapshot *snst = op->snst;
	uint64_t last_ev = snst->events[snst->events_wp];
	double new_centering;
	if(last_ev) {
		double sweep;
		if(snst->calibrate)
			sweep = (double) snst->nominal_sr / PAPERSTRIP_ZOOM_CAL;
		else
			sweep = snst->sample_rate * 3600. / (PAPERSTRIP_ZOOM * snst->guessed_bph);
		new_centering = fmod(last_ev + .5*sweep , sweep);
	} else 
		new_centering = 0;
	snst->trace_centering = new_centering;
	gtk_widget_queue_draw(op->paperstrip_drawing_area);
}

static void shift_trace(struct output_panel *op, double direction)
{
	struct snapshot *snst = op->snst;
	double sweep;
	if(snst->calibrate)
		sweep = (double) snst->nominal_sr / PAPERSTRIP_ZOOM_CAL;
	else
		sweep = snst->sample_rate * 3600. / (PAPERSTRIP_ZOOM * snst->guessed_bph);
	snst->trace_centering = fmod(snst->trace_centering + sweep * (1.+.1*direction), sweep);
	gtk_widget_queue_draw(op->paperstrip_drawing_area);
}

static void handle_left(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	shift_trace(op,-1);
}

static void handle_right(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	shift_trace(op,1);
}

void op_set_snapshot(struct output_panel *op, struct snapshot *snst)
{
	op->snst = snst;
	gtk_widget_set_sensitive(op->clear_button, !snst->calibrate);
}

void op_set_border(struct output_panel *op, int i)
{
	gtk_container_set_border_width(GTK_CONTAINER(op->panel), i);
}

void op_destroy(struct output_panel *op)
{
	snapshot_destroy(op->snst);
	free(op);
}

struct output_panel *init_output_panel(struct computer *comp, struct snapshot *snst, int border)
{
	struct output_panel *op = malloc(sizeof(struct output_panel));

	op->auto_measure_state = 0;
	op->auto_measure_countdown = 0;
	op->auto_measure_timer = 0;
	op->manual_measure_target = -1;
	op->manual_measure_countdown = 0;
	op->manual_measure_timer = 0;
	for (int i = 0; i < 8; i++) op->manual_measure_buttons[i] = NULL;
	op->num_custom = 0;
	op->winder_active = 0;
	op->winder_state = 0;
	op->winder_countdown = 0;
	op->winder_cycles = 0;

	op->computer = comp;
	op->snst = snst;

	op->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(op->panel), border);

	// output_drawing_area는 vbox3으로 이동 (아래에서 생성)
	op->output_drawing_area = gtk_drawing_area_new();
	g_signal_connect (op->output_drawing_area, "draw", G_CALLBACK(output_draw_event), op);
	gtk_widget_set_events(op->output_drawing_area, GDK_EXPOSURE_MASK);

	GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(op->panel), hbox2, TRUE, TRUE, 0);

	GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start(GTK_BOX(hbox2), vbox2, FALSE, TRUE, 0);

	// Paperstrip
	op->paperstrip_drawing_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(op->paperstrip_drawing_area, 300, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), op->paperstrip_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->paperstrip_drawing_area, "draw", G_CALLBACK(paperstrip_draw_event), op);
	gtk_widget_set_events(op->paperstrip_drawing_area, GDK_EXPOSURE_MASK);

	GtkWidget *hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(vbox2), hbox3, FALSE, TRUE, 0);

	// < button
	GtkWidget *left_button = gtk_button_new_with_label("<");
	gtk_box_pack_start(GTK_BOX(hbox3), left_button, TRUE, TRUE, 0);
	g_signal_connect (left_button, "clicked", G_CALLBACK(handle_left), op);

	// CLEAR button
	if(comp) {
		op->clear_button = gtk_button_new_with_label(_("Clear"));
		gtk_box_pack_start(GTK_BOX(hbox3), op->clear_button, TRUE, TRUE, 0);
		g_signal_connect (op->clear_button, "clicked", G_CALLBACK(handle_clear_trace), op);
		gtk_widget_set_sensitive(op->clear_button, !snst->calibrate);
	}

	// CENTER button
	GtkWidget *center_button = gtk_button_new_with_label(_("Center"));
	gtk_box_pack_start(GTK_BOX(hbox3), center_button, TRUE, TRUE, 0);
	g_signal_connect (center_button, "clicked", G_CALLBACK(handle_center_trace), op);

	// > button
	GtkWidget *right_button = gtk_button_new_with_label(">");
	gtk_box_pack_start(GTK_BOX(hbox3), right_button, TRUE, TRUE, 0);
	g_signal_connect (right_button, "clicked", G_CALLBACK(handle_right), op);

	GtkWidget *vbox3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start(GTK_BOX(hbox2), vbox3, TRUE, TRUE, 0);

	// ── 측정값 표시 — 최소 크기만 지정, 실제 크기는 hrow_top 높이에 따라 확장 ──
	gtk_widget_set_size_request(op->output_drawing_area, 350, 180);

	// Tic waveform area
	// op->tic_drawing_area = gtk_drawing_area_new();
	// gtk_box_pack_start(GTK_BOX(vbox3), op->tic_drawing_area, TRUE, TRUE, 0);
	// g_signal_connect (op->tic_drawing_area, "draw", G_CALLBACK(tic_draw_event), op);
	// gtk_widget_set_events(op->tic_drawing_area, GDK_EXPOSURE_MASK);

	// Toc waveform area
	// op->toc_drawing_area = gtk_drawing_area_new();
	// gtk_box_pack_start(GTK_BOX(vbox3), op->toc_drawing_area, TRUE, TRUE, 0);
	// g_signal_connect (op->toc_drawing_area, "draw", G_CALLBACK(toc_draw_event), op);
	// gtk_widget_set_events(op->toc_drawing_area, GDK_EXPOSURE_MASK);

	// ── CSS 스타일 ─────────────────────────────────────────────────────
	{
		GtkCssProvider *css = gtk_css_provider_new();
		const char *css_data =
			/* 전체 레이블 기본 크기 */
			"label { font-size: 14px; }\n"
			/* 프레임 제목 */
			".pos-frame > label { font-size: 15px; font-weight: bold;"
			"  color: #4fc3f7; letter-spacing: 1px; }\n"
			/* 자세 이름 레이블 */
			".pos-name { font-size: 14px; font-weight: bold; color: #90caf9;"
			"  min-width: 115px; }\n"
			/* 측정값 레이블 */
			".pos-value { font-size: 14px; color: #e0f0ff;"
			"  background-color: rgba(20,35,60,0.9);"
			"  border-radius: 5px; padding: 4px 12px; min-width: 330px; }\n"
			/* Auto Measure 버튼 */
			".btn-auto { font-size: 15px; font-weight: bold;"
			"  background-image: none; background-color: #1565c0;"
			"  color: white; border-radius: 8px; min-height: 40px; padding: 0 18px; }\n"
			".btn-auto:hover { background-color: #1976d2; }\n"
			".btn-auto:active { background-color: #0d47a1; }\n"
			/* Base 버튼 */
			".btn-base { font-size: 14px;"
			"  background-image: none; background-color: #263238;"
			"  color: #cfd8dc; border-radius: 8px; min-height: 40px; padding: 0 14px; }\n"
			".btn-base:hover { background-color: #37474f; }\n"
			/* Test Custom 버튼 */
			".btn-test { font-size: 13px;"
			"  background-image: none; background-color: #1b5e20;"
			"  color: #a5d6a7; border-radius: 6px; min-height: 34px; padding: 0 12px; }\n"
			".btn-test:hover { background-color: #2e7d32; }\n"
			/* 스핀버튼 */
			"spinbutton { font-size: 14px; min-height: 34px; min-width: 88px; }\n"
			/* 스핀 앞 레이블 */
			".spin-label { font-size: 13px; color: #b0bec5; }\n"
			/* 삭제 버튼 */
			".btn-del { font-size: 12px;"
			"  background-image: none; background-color: #7f0000;"
			"  color: #ffcdd2; border-radius: 5px;"
			"  min-height: 26px; min-width: 26px; padding: 0 4px; }\n"
			".btn-del:hover { background-color: #b71c1c; }\n"
			/* 와치와인더 버튼 */
			".btn-winder { font-size: 15px; font-weight: bold;"
			"  background-image: none; background-color: #4a148c;"
			"  color: #e1bee7; border-radius: 8px;"
			"  min-height: 44px; padding: 0 18px; }\n"
			".btn-winder:hover { background-color: #6a1b9a; }\n"
			".btn-winder-stop { font-size: 15px; font-weight: bold;"
			"  background-image: none; background-color: #880e4f;"
			"  color: #fce4ec; border-radius: 8px;"
			"  min-height: 44px; padding: 0 18px; }\n"
			".btn-winder-stop:hover { background-color: #ad1457; }\n"
			/* 진단 리포트 텍스트뷰 */
			".analysis-view { background-color: #080e1c; color: #b8cce0;"
			"  font-family: monospace; font-size: 13px; }\n"
			".analysis-view > * { background-color: transparent; }\n"
			/* Watch Winder / Positional Error 내부 배경 */
			".dark-inner { background-color: #12121e; }\n"
			".dark-inner label { color: #cfd8dc; background-color: transparent; }\n"
			/* 콤보박스 다크 스타일 */
			".dark-inner combobox button { background-image: none; background-color: #1e2a3a;"
			"  color: #e0f0ff; border-color: #2a3a5a; }\n"
			".dark-inner combobox button:hover { background-color: #263545; }\n"
			/* 프레임 테두리 색상 (흰색 방지) */
			".pos-frame { border-color: #2a3a5a; }\n"
			".pos-frame border { background-color: #2a3a5a; }\n";
		gtk_css_provider_load_from_data(css, css_data, -1, NULL);
		gtk_style_context_add_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(css),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(css);
	}

	// ── Positional Error UI ────────────────────────────────────────────
	GtkWidget *pos_frame = gtk_frame_new(" ⊙  Positional Error — Auto Measure ");
	gtk_style_context_add_class(gtk_widget_get_style_context(pos_frame), "pos-frame");
	// pos_frame은 hrow_top에서 output_drawing_area와 가로로 배치 (아래 참고)
	
	GtkWidget *pos_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(pos_vbox), "dark-inner");
	gtk_container_set_border_width(GTK_CONTAINER(pos_vbox), 10);
	gtk_container_add(GTK_CONTAINER(pos_frame), pos_vbox);

	GtkWidget *pos_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(pos_grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(pos_grid), 12);
	gtk_box_pack_start(GTK_BOX(pos_vbox), pos_grid, FALSE, FALSE, 0);

	// 고정 4자세 행 (항상 표시)
	const char* pos_names[] = {_("🕘 9시 (기본)"), _("🕛 12시"), _("🕒 3시"), _("🕕 6시")};
	for (int i = 0; i < 4; i++) {
		GtkWidget *lbl_name = gtk_label_new(pos_names[i]);
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl_name), "pos-name");
		gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);

		op->pos_labels[i] = gtk_label_new(_("Rate: -- s/d  |  Amp: --°  |  BE: -- ms"));
		gtk_style_context_add_class(gtk_widget_get_style_context(op->pos_labels[i]), "pos-value");
		gtk_widget_set_halign(op->pos_labels[i], GTK_ALIGN_START);

		// 고정 자세 측정 버튼 (커스텀과 동일하게 행 오른쪽에 배치)
		GtkWidget *measure_btn = gtk_button_new_with_label(_("측정"));
		gtk_style_context_add_class(gtk_widget_get_style_context(measure_btn), "btn-test");
		g_object_set_data(G_OBJECT(measure_btn), "manual_target", GINT_TO_POINTER(i)); // 0..3
		extern void on_manual_measure_clicked(GtkWidget *widget, gpointer data);
		g_signal_connect(measure_btn, "clicked", G_CALLBACK(on_manual_measure_clicked), op);
		op->manual_measure_buttons[i] = measure_btn;

		// 그리드: [이름] [측정값] [측정버튼]
		gtk_grid_attach(GTK_GRID(pos_grid), lbl_name,          0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(pos_grid), op->pos_labels[i], 1, i, 1, 1);
		gtk_grid_attach(GTK_GRID(pos_grid), measure_btn,       2, i, 1, 1);
	}

	// 커스텀 4자세 행 (초기에는 숨김 → 추가 버튼 누를 때 표시)
	extern void on_delete_custom_clicked(GtkWidget *widget, gpointer data);
	for (int c = 0; c < 4; c++) {
		int row = 4 + c;

		op->custom_name_labels[c] = gtk_label_new(_("★ 커스텀 --"));
		gtk_style_context_add_class(gtk_widget_get_style_context(op->custom_name_labels[c]), "pos-name");
		gtk_widget_set_halign(op->custom_name_labels[c], GTK_ALIGN_START);

		op->pos_labels[row] = gtk_label_new(_("Rate: -- s/d  |  Amp: --°  |  BE: -- ms"));
		gtk_style_context_add_class(gtk_widget_get_style_context(op->pos_labels[row]), "pos-value");
		gtk_widget_set_halign(op->pos_labels[row], GTK_ALIGN_START);

		// 커스텀 측정 버튼 (해당 커스텀 위치로 이동 후 측정값 저장)
		GtkWidget *measure_btn = gtk_button_new_with_label(_("측정"));
		gtk_style_context_add_class(gtk_widget_get_style_context(measure_btn), "btn-test");
		g_object_set_data(G_OBJECT(measure_btn), "manual_target", GINT_TO_POINTER(row)); // 4..7
		extern void on_manual_measure_clicked(GtkWidget *widget, gpointer data);
		g_signal_connect(measure_btn, "clicked", G_CALLBACK(on_manual_measure_clicked), op);
		op->manual_measure_buttons[row] = measure_btn;

		// 삭제 버튼
		GtkWidget *del_btn = gtk_button_new_with_label("✕");
		gtk_style_context_add_class(gtk_widget_get_style_context(del_btn), "btn-del");
		g_object_set_data(G_OBJECT(del_btn), "row_idx", GINT_TO_POINTER(c));
		g_signal_connect(del_btn, "clicked", G_CALLBACK(on_delete_custom_clicked), op);

		// 전체 행: [이름] [측정값] [측정버튼] [삭제버튼]
		GtkWidget *full_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_box_pack_start(GTK_BOX(full_row), op->custom_name_labels[c], FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(full_row), op->pos_labels[row],       TRUE,  TRUE,  0);
		gtk_box_pack_end  (GTK_BOX(full_row), measure_btn,               FALSE, FALSE, 0);
		gtk_box_pack_end  (GTK_BOX(full_row), del_btn,                   FALSE, FALSE, 0);
		op->custom_rows[c] = full_row;

		gtk_grid_attach(GTK_GRID(pos_grid), full_row, 0, row, 2, 1);
		gtk_widget_set_no_show_all(full_row, TRUE); // 처음엔 숨김
	}

	// 버튼 행
	GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(pos_vbox), btn_box, FALSE, FALSE, 4);

	GtkWidget *base_button = gtk_button_new_with_label(_("⏎  Base (9시)"));
	gtk_style_context_add_class(gtk_widget_get_style_context(base_button), "btn-base");
	gtk_box_pack_start(GTK_BOX(btn_box), base_button, TRUE, TRUE, 0);
	extern void on_base_clicked(GtkWidget *widget, gpointer data);
	g_signal_connect(base_button, "clicked", G_CALLBACK(on_base_clicked), op);

	op->auto_measure_button = gtk_button_new_with_label(_("▶  자세차 자동 측정"));
	gtk_style_context_add_class(gtk_widget_get_style_context(op->auto_measure_button), "btn-auto");
	gtk_box_pack_start(GTK_BOX(btn_box), op->auto_measure_button, TRUE, TRUE, 0);
	extern void on_auto_measure_clicked(GtkWidget *widget, gpointer data);
	g_signal_connect(op->auto_measure_button, "clicked", G_CALLBACK(on_auto_measure_clicked), op);

	// 커스텀 포지션 행
	GtkWidget *custom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(pos_vbox), custom_box, FALSE, FALSE, 2);

	GtkWidget *lbl_custom = gtk_label_new(_("커스텀 위치:"));
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_custom), "spin-label");
	gtk_box_pack_start(GTK_BOX(custom_box), lbl_custom, FALSE, FALSE, 0);

	extern GtkWidget *custom_id1_spin;
	extern GtkWidget *custom_id2_spin;

	GtkWidget *lbl_id1 = gtk_label_new(_("ID1 (Face)"));
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_id1), "spin-label");
	gtk_box_pack_start(GTK_BOX(custom_box), lbl_id1, FALSE, FALSE, 0);
	custom_id1_spin = gtk_spin_button_new_with_range(FACE_MIN, FACE_MAX, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(custom_id1_spin), 2992);
	gtk_box_pack_start(GTK_BOX(custom_box), custom_id1_spin, FALSE, FALSE, 0);
	extern void on_custom_spin_changed(GtkWidget *widget, gpointer data);
	g_signal_connect(custom_id1_spin, "value-changed", G_CALLBACK(on_custom_spin_changed), op);

	GtkWidget *lbl_id2 = gtk_label_new(_("ID2 (Arm)"));
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_id2), "spin-label");
	gtk_box_pack_start(GTK_BOX(custom_box), lbl_id2, FALSE, FALSE, 0);
	custom_id2_spin = gtk_spin_button_new_with_range(ARM_MIN, ARM_MAX, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(custom_id2_spin), 108);
	gtk_box_pack_start(GTK_BOX(custom_box), custom_id2_spin, FALSE, FALSE, 0);
	g_signal_connect(custom_id2_spin, "value-changed", G_CALLBACK(on_custom_spin_changed), op);

	GtkWidget *add_custom_btn = gtk_button_new_with_label(_("＋  커스텀 위치 추가"));
	gtk_style_context_add_class(gtk_widget_get_style_context(add_custom_btn), "btn-test");
	gtk_box_pack_start(GTK_BOX(custom_box), add_custom_btn, FALSE, FALSE, 0);
	extern void on_add_custom_clicked(GtkWidget *widget, gpointer data);
	g_signal_connect(add_custom_btn, "clicked", G_CALLBACK(on_add_custom_clicked), op);

	// ── 저장된 커스텀 자세 불러오기 ────────────────────────────────────
	{
		extern int load_custom_positions(struct output_panel *op);
		load_custom_positions(op);
	}

	// ── Watch Winder UI (hrow_top 오른쪽, 흰 공간 채움) ─────────────────
	{
		GtkWidget *winder_frame = gtk_frame_new(" 🔄  Watch Winder ");
		gtk_style_context_add_class(gtk_widget_get_style_context(winder_frame), "pos-frame");

		// Positional Error(자세차+커스텀) 영역을 스크롤 가능하게 → 작은 창/해상도에서도 커스텀 위치 행이 보이도록
		GtkWidget *pos_scroll = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pos_scroll),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		/* 스크롤 없이(기본 4자세 + 커스텀 4줄 + 버튼들) 한 화면에 보이도록 높이 확보 */
		gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(pos_scroll), 440);
		gtk_widget_set_size_request(pos_scroll, 420, -1);
		gtk_container_add(GTK_CONTAINER(pos_scroll), pos_frame);

		// hrow_top: pos_scroll(왼쪽, 자세차+커스텀) + winder_frame(오른쪽, 확장)
		GtkWidget *hrow_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		// 작은 해상도에서도 자세차/커스텀 영역이 너무 작아지지 않도록 양쪽 모두 세로로 확장
		gtk_box_pack_start(GTK_BOX(hrow_top), pos_scroll,   TRUE,  TRUE,  0);
		gtk_box_pack_start(GTK_BOX(hrow_top), winder_frame, TRUE,  TRUE,  0);
		gtk_box_pack_start(GTK_BOX(vbox3),    hrow_top,     TRUE,  TRUE,  0);

		GtkWidget *winder_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		gtk_style_context_add_class(gtk_widget_get_style_context(winder_vbox), "dark-inner");
		gtk_container_set_border_width(GTK_CONTAINER(winder_vbox), 14);
		gtk_container_add(GTK_CONTAINER(winder_frame), winder_vbox);

		// ── 프리셋 선택 행 ──
		GtkWidget *preset_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_box_pack_start(GTK_BOX(winder_vbox), preset_hbox, FALSE, FALSE, 0);

		GtkWidget *preset_lbl = gtk_label_new(_("프리셋:"));
		gtk_style_context_add_class(gtk_widget_get_style_context(preset_lbl), "spin-label");
		gtk_box_pack_start(GTK_BOX(preset_hbox), preset_lbl, FALSE, FALSE, 0);

		op->winder_preset_combo = gtk_combo_box_text_new();
		for (int i = 0; i < WINDER_PRESET_COUNT; i++)
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(op->winder_preset_combo),
			                               _(winder_presets[i].name));
		gtk_combo_box_set_active(GTK_COMBO_BOX(op->winder_preset_combo), 0);
		gtk_box_pack_start(GTK_BOX(preset_hbox), op->winder_preset_combo, TRUE, TRUE, 0);

		// ── 상태 레이블 ──
		op->winder_status_label = gtk_label_new(_("대기 중 — 프리셋을 선택하고 시작하세요"));
		gtk_style_context_add_class(gtk_widget_get_style_context(op->winder_status_label), "pos-value");
		gtk_widget_set_halign(op->winder_status_label, GTK_ALIGN_START);
		gtk_label_set_line_wrap(GTK_LABEL(op->winder_status_label), TRUE);
		gtk_box_pack_start(GTK_BOX(winder_vbox), op->winder_status_label, FALSE, FALSE, 0);

		// ── 시작/정지 버튼 ──
		op->winder_button = gtk_button_new_with_label(_("🔄  와치와인더 시작"));
		gtk_style_context_add_class(gtk_widget_get_style_context(op->winder_button), "btn-winder");
		gtk_box_pack_start(GTK_BOX(winder_vbox), op->winder_button, FALSE, FALSE, 0);
		extern void on_winder_clicked(GtkWidget *widget, gpointer data);
		g_signal_connect(op->winder_button, "clicked", G_CALLBACK(on_winder_clicked), op);

		// ── 진단 리포트 텍스트뷰 (자세차 측정 완료 후 표시) ──
		op->analysis_scroll = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(
			GTK_SCROLLED_WINDOW(op->analysis_scroll),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_widget_set_size_request(op->analysis_scroll, -1, 80);

		op->analysis_textview = gtk_text_view_new();
		gtk_text_view_set_editable(GTK_TEXT_VIEW(op->analysis_textview), FALSE);
		gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(op->analysis_textview), FALSE);
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(op->analysis_textview), GTK_WRAP_WORD_CHAR);
		gtk_text_view_set_left_margin(GTK_TEXT_VIEW(op->analysis_textview), 14);
		gtk_text_view_set_right_margin(GTK_TEXT_VIEW(op->analysis_textview), 14);
		gtk_text_view_set_top_margin(GTK_TEXT_VIEW(op->analysis_textview), 10);
		gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(op->analysis_textview), 10);
		gtk_style_context_add_class(
			gtk_widget_get_style_context(op->analysis_textview), "analysis-view");
		gtk_container_add(GTK_CONTAINER(op->analysis_scroll), op->analysis_textview);
		gtk_box_pack_start(GTK_BOX(winder_vbox), op->analysis_scroll, TRUE, TRUE, 0);
		gtk_widget_set_no_show_all(op->analysis_scroll, TRUE);
		// 앱 시작 시 진단 리포트 영역을 바로 표시 (미측정 문구로)
		{
			GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(op->analysis_textview));
			char *init_t = g_strdup_printf(
				"╔═══════════════════════════════════════════╗\n"
				"%s"
				"╚═══════════════════════════════════════════╝\n\n"
				"%s"
				"%s",
				_("║    🕰  MrWatchmaker  자세차 진단 리포트    ║\n"),
				_("  — 종합 등급: ( 미측정 — 자세별 측정 후 표시됩니다 )\n\n"),
				_("  측정 데이터가 없어 상세 분석을 표시할 수 없습니다.\n  자세별 측정을 완료하면 등급과 분석이 표시됩니다.\n"));
			gtk_text_buffer_set_text(buf, init_t, -1);
			g_free(init_t);
			gtk_widget_set_no_show_all(op->analysis_scroll, FALSE);
			gtk_widget_show_all(op->analysis_scroll);
		}
	}

	// ── 측정값 표시 (Watch Winder 아래, 높이는 위젯 할당 크기에 따라 자동 스케일) ──
	gtk_box_pack_start(GTK_BOX(vbox3), op->output_drawing_area, FALSE, FALSE, 0);

	// ── Period waveform: vbox3 하단에 배치 (오른쪽 컬럼 안, 남는 공간 모두 사용) ──
	op->period_drawing_area = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(vbox3), op->period_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->period_drawing_area, "draw", G_CALLBACK(period_draw_event), op);
	gtk_widget_set_events(op->period_drawing_area, GDK_EXPOSURE_MASK);

#ifdef DEBUG
	op->debug_drawing_area = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(vbox3), op->debug_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->debug_drawing_area, "draw", G_CALLBACK(debug_draw_event), op);
	gtk_widget_set_events(op->debug_drawing_area, GDK_EXPOSURE_MASK);
#endif

	return op;
}

// Auto measure state machine
// Face motor (ID 1) positions: 9, 12, 3, 6, Custom
// 9시 방향 (기본 자세) : 3044,987
// 12시 방향  3047 , 1991
// 3시 방향    3047 , 3005
// 6시 방향  4093 ,  2015
// 추가 자세차    4084 , 2014
// 고정 자세: 9시=0, 12시=1, 3시=2, 6시=3
static int face_positions[] = {1975, 2997, 4010, 2997};
static int arm_positions[]  = {1079, 2070, 1081, 125};

static int clamp_face_hard(int v) {
	if (v < FACE_MIN) return FACE_MIN;
	if (v > FACE_MAX) return FACE_MAX;
	return v;
}
static int clamp_arm_hard(int v) {
	if (v < ARM_MIN) return ARM_MIN;
	if (v > ARM_MAX) return ARM_MAX;
	return v;
}

static int clamp_face_soft(int v) {
	const int minv = FACE_MIN + FACE_SOFT_MARGIN;
	const int maxv = FACE_MAX - FACE_SOFT_MARGIN;
	if (v < minv) return minv;
	if (v > maxv) return maxv;
	return v;
}
static int clamp_arm_soft(int v) {
	const int minv = ARM_MIN + ARM_SOFT_MARGIN;
	const int maxv = ARM_MAX - ARM_SOFT_MARGIN;
	if (v < minv) return minv;
	if (v > maxv) return maxv;
	return v;
}

static int lerp_int(int a, int b, int num, int den) {
	/* a + (b-a)*num/den (정수) */
	return a + (int)((long long)(b - a) * num / den);
}

/* 와인더 전용: 구간 시간 계산 */
static int winder_calc_duration(int from, int to) {
	int dist = from - to;
	if (dist < 0) dist = -dist;
	int ms = WINDER_MIN_MS + (dist * 2);
	if (ms > WINDER_MAX_MS) ms = WINDER_MAX_MS;
	return ms;
}

static int clamp_face(int v) {
	return clamp_face_hard(v);
}
static int clamp_arm(int v) {
	return clamp_arm_hard(v);
}

static const char *winder_pos_name[] = {"9시", "12시", "3시", "6시"};

// 커스텀 자세 (최대 4개, 동적 추가)
static int custom_face_pos[4] = {0, 0, 0, 0};
static int custom_arm_pos[4]  = {0, 0, 0, 0};

// 이동 거리에 따라 속도를 자동 계산 (최소 5초, 거리가 클수록 더 느리게)
static int calc_duration(int from, int to) {
	int dist = from - to;
	if (dist < 0) dist = -dist;
	int ms = 5000 + (dist * 3); // 거리 1당 3ms 추가 (최소 5초)
	if (ms > 12000) ms = 12000; // 최대 12초
	return ms;
}

/* 서보 토크가 꺼져 있으면(예: 9시 이동 후 걸림 감지로 OFF) 특정 자세에서 힘이 없고
 * 떨림/밀림이 생길 수 있음. 이동/와인더 시작 때마다 토크를 확실히 ON으로 복구한다. */
static void motor_ensure_torque_on(void) {
	motor_write_byte(1, 0x28, 1); /* Torque Enable = 1 */
	motor_write_byte(2, 0x28, 1); /* Torque Enable = 1 */
}

// 마지막으로 이동한 위치 기억 (속도 계산용)
static int last_pos1 = 1975;
static int last_pos2 = 1079;

/* ── 수동 자세별 측정(버튼): 해당 자세로 이동 후 카운트다운 뒤 값 저장 ───────── */
typedef struct { struct output_panel *op; int target; } manual_move_data_t;

static void set_manual_buttons_enabled(struct output_panel *op, int enabled) {
	for (int i = 0; i < 8; i++) {
		if (op->manual_measure_buttons[i])
			gtk_widget_set_sensitive(op->manual_measure_buttons[i], enabled);
	}
	if (op->auto_measure_button)
		gtk_widget_set_sensitive(op->auto_measure_button, enabled);
}

static void manual_reset_button_labels(struct output_panel *op) {
	static const char *fixed_names[4] = {"🕘 9시 측정", "🕛 12시 측정", "🕒 3시 측정", "🕕 6시 측정"};
	for (int i = 0; i < 4; i++)
		if (op->manual_measure_buttons[i])
			gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[i]), _(fixed_names[i]));
	for (int c = 0; c < 4; c++) {
		int idx = 4 + c;
		if (op->manual_measure_buttons[idx]) {
			char buf[32];
			snprintf(buf, sizeof(buf), _("★%d 측정"), c + 1);
			gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[idx]), buf);
		}
	}
}

static gboolean manual_measure_tick(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	if (op->manual_measure_target < 0 || op->manual_measure_target > 7)
		return G_SOURCE_REMOVE;

	op->manual_measure_countdown--;
	if (op->manual_measure_countdown > 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), _("측정 중... %d 초"), op->manual_measure_countdown);
		GtkWidget *btn = op->manual_measure_buttons[op->manual_measure_target];
		if (btn) gtk_button_set_label(GTK_BUTTON(btn), buf);
		return G_SOURCE_CONTINUE;
	}

	/* 카운트다운 종료 → 현재 snapshot 값을 해당 자세 칸에 기록 */
	{
		int idx = op->manual_measure_target;
		char buf[128];
		double rate = op->snst->rate;
		double amp  = op->snst->amp;
		double be   = op->snst->be;
		snprintf(buf, sizeof(buf), "Rate: %.1f s/d  |  Amp: %.0f°  |  BE: %.1f ms", rate, amp, be);
		gtk_label_set_text(GTK_LABEL(op->pos_labels[idx]), buf);
		op->pos_rate[idx]     = rate;
		op->pos_amp[idx]      = amp;
		op->pos_be[idx]       = be;
		op->pos_measured[idx] = 1;
		/* 진단 리포트 갱신 */
		extern void generate_analysis(struct output_panel *op);
		generate_analysis(op);
	}

	manual_reset_button_labels(op);
	set_manual_buttons_enabled(op, 1);
	op->manual_measure_target = -1;
	return G_SOURCE_REMOVE;
}

static gboolean manual_after_move_idle(gpointer data) {
	manual_move_data_t *md = (manual_move_data_t *)data;
	struct output_panel *op = md->op;
	op->manual_measure_target = md->target;
	op->manual_measure_countdown = 20;
	op->manual_measure_timer = g_timeout_add_seconds(1, manual_measure_tick, op);
	g_free(md);
	return G_SOURCE_REMOVE;
}

static gpointer manual_move_thread_func(gpointer data) {
	manual_move_data_t *md = (manual_move_data_t *)data;
	int t = md->target;
	if (t < 0 || t > 7) return NULL;

	int p1, p2;
	if (t < 4) {
		p1 = clamp_face(face_positions[t]);
		p2 = clamp_arm(arm_positions[t]);
	} else {
		int c = t - 4;
		p1 = clamp_face(custom_face_pos[c]);
		p2 = clamp_arm(custom_arm_pos[c]);
	}
	int dur1 = calc_duration(last_pos1, p1);
	int dur2 = calc_duration(last_pos2, p2);

	motor_init(motor_get_port());
	motor_ensure_torque_on();
	motor_move(1, p1, dur1, 0);
	g_usleep(100000);
	motor_move(2, p2, dur2, 0);
	motor_close();

	last_pos1 = p1;
	last_pos2 = p2;

	/* 이동 후 메인 스레드에서 카운트다운 시작 */
	g_idle_add(manual_after_move_idle, md);
	return NULL;
}

void on_manual_measure_clicked(GtkWidget *widget, gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	if (op->manual_measure_target != -1) return; /* 진행 중이면 무시 */

	int target = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "manual_target"));
	if (target < 0 || target > 7) return;

	if (!motor_init(motor_get_port())) {
		GtkWidget *win = gtk_widget_get_toplevel(op->panel);
		if (!GTK_IS_WINDOW(win)) win = NULL;
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
			GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, NULL);
		gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg),
			_("서보모터가 연결되어 있지 않거나 <b>aitimebot</b>이 필요합니다.\n<a href=\"http://mrwatchmaker.com\">mrwatchmaker.com</a> 에서 구매해 주세요."));
		gtk_window_set_title(GTK_WINDOW(dlg), _("장치 연결 오류"));
		if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		return;
	}
	motor_close();

	set_manual_buttons_enabled(op, 0);
	manual_reset_button_labels(op);
	if (op->manual_measure_buttons[target])
		gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[target]), _("이동 중..."));

	manual_move_data_t *md = g_new(manual_move_data_t, 1);
	md->op = op;
	md->target = target;
	g_thread_new("manual_measure_move", manual_move_thread_func, md);
}

// 자세차 순서 프리셋: 슬롯 인덱스(0=9시..3=6시, 4+=커스텀) → face/arm 위치
static void get_positional_slot_face_arm(struct output_panel *op, int slot_idx, int *out_p1, int *out_p2) {
	if (slot_idx < 4) {
		*out_p1 = face_positions[slot_idx];
		*out_p2 = arm_positions[slot_idx];
	} else {
		int c = slot_idx - 4;
		*out_p1 = (c < op->num_custom) ? custom_face_pos[c] : face_positions[0];
		*out_p2 = (c < op->num_custom) ? custom_arm_pos[c]  : arm_positions[0];
	}
}

// 자세차 순서 프리셋: 슬롯 인덱스 → 표시 이름 (상태 라벨용)
static const char *get_positional_slot_name(struct output_panel *op, int slot_idx) {
	static char buf[32];
	(void)op;
	if (slot_idx < 4) return _(winder_pos_name[slot_idx]);
	snprintf(buf, sizeof(buf), _("커스텀 %d"), slot_idx - 3);
	return buf;
}

/* 커스텀만 반복 프리셋: 슬롯 인덱스 → 표시 이름 */
static const char *get_custom_only_slot_name(int slot_idx) {
	static char buf[32];
	snprintf(buf, sizeof(buf), _("커스텀 %d"), slot_idx + 1);
	return buf;
}

/* 와인더 연속 궤적(trajectory)용: 현재 스텝의 목표(face/arm) 계산 */
static void winder_compute_step_targets(struct output_panel *op, int *out_p1, int *out_p2) {
	const WinderPreset *p = &winder_presets[op->winder_preset];
	int total, pos_idx, p1, p2;

	if (op->winder_preset == WINDER_PRESET_POSITIONAL) {
		total = 4 + op->num_custom;
		if (total <= 0) total = 1;
		pos_idx = op->winder_state % total;
		get_positional_slot_face_arm(op, pos_idx, &p1, &p2);
	} else if (op->winder_preset == WINDER_PRESET_CUSTOM_ONLY) {
		total = op->num_custom;
		if (total <= 0) total = 1;
		pos_idx = op->winder_state % total;
		p1 = custom_face_pos[pos_idx];
		p2 = custom_arm_pos[pos_idx];
	} else {
		total = p->len;
		pos_idx = p->seq[op->winder_state % p->len];
		p1 = face_positions[pos_idx];
		p2 = arm_positions[pos_idx];
	}

	*out_p1 = clamp_face_hard(p1);
	*out_p2 = clamp_arm_hard(p2);
}

static void winder_begin_segment(struct output_panel *op) {
	int p1, p2;
	winder_compute_step_targets(op, &p1, &p2);

	op->winder_from1 = last_pos1;
	op->winder_from2 = last_pos2;
	op->winder_to1   = p1;
	op->winder_to2   = p2;
	op->winder_seg_elapsed_ms = 0;

	int dur1 = winder_calc_duration(op->winder_from1, op->winder_to1);
	int dur2 = winder_calc_duration(op->winder_from2, op->winder_to2);
	op->winder_seg_total_ms = (dur1 > dur2 ? dur1 : dur2);
	if (op->winder_seg_total_ms < WINDER_MIN_MS) op->winder_seg_total_ms = WINDER_MIN_MS;
	if (op->winder_seg_total_ms > WINDER_MAX_MS) op->winder_seg_total_ms = WINDER_MAX_MS;
}

GtkWidget *custom_id1_spin = NULL;
GtkWidget *custom_id2_spin = NULL;

void on_custom_spin_changed(GtkWidget *widget, gpointer data) {
	(void)data;
	motor_init(motor_get_port());
	motor_ensure_torque_on();
	if (widget == custom_id1_spin) {
		int id1_val = clamp_face(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom_id1_spin)));
		motor_move(1, id1_val, 1500, 0); // 스핀 미세조정: 1.5초 고정
		last_pos1 = id1_val;
	} else if (widget == custom_id2_spin) {
		int id2_val = clamp_arm(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom_id2_spin)));
		motor_move(2, id2_val, 1500, 0); // 스핀 미세조정: 1.5초 고정
		last_pos2 = id2_val;
	}
	motor_close();
}

void on_test_custom_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;
	int id1_val = clamp_face(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom_id1_spin)));
	int id2_val = clamp_arm(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom_id2_spin)));
	int dur1 = calc_duration(last_pos1, id1_val);
	int dur2 = calc_duration(last_pos2, id2_val);
	
	if (!motor_init(motor_get_port())) {
		GtkWidget *win = gtk_widget_get_toplevel(op->panel);
		if (!GTK_IS_WINDOW(win)) win = NULL;
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
			GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, NULL);
		gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg),
			_("서보모터가 연결되어 있지 않거나 <b>aitimebot</b>이 필요합니다.\n<a href=\"http://mrwatchmaker.com\">mrwatchmaker.com</a> 에서 구매해 주세요."));
		gtk_window_set_title(GTK_WINDOW(dlg), _("장치 연결 오류"));
		if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		return;
	}
	motor_ensure_torque_on();
	motor_move(1, id1_val, dur1, 0);
	g_usleep(100000);
	motor_move(2, id2_val, dur2, 0);
	last_pos1 = id1_val;
	last_pos2 = id2_val;
	motor_close();
}

/* 9시 이동+쳐박힘 체크 스레드 종료 후 메인 스레드에서 호출 (g_idle_add) */
typedef struct { int released; struct output_panel *op; } base_done_data_t;

static gboolean on_base_done_idle(gpointer data) {
	base_done_data_t *bd = (base_done_data_t *)data;
	last_pos1 = face_positions[0];
	last_pos2 = arm_positions[0];
	if (bd->released) {
		GtkWidget *win = gtk_widget_get_toplevel(bd->op->panel);
		if (!GTK_IS_WINDOW(win)) win = NULL;
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
			GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
			_("암이 걸렸습니다. 나사 풀지 말고 손으로 살짝 돌려 맞춘 뒤 [Base (9시)]를 다시 누르세요."));
		gtk_window_set_title(GTK_WINDOW(dlg), _("암 토크 해제"));
		if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
	}
	g_free(bd);
	return G_SOURCE_REMOVE;
}

static gpointer base_move_thread_func(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	int dur1 = calc_duration(last_pos1, face_positions[0]);
	int dur2 = calc_duration(last_pos2, arm_positions[0]);
	motor_init(motor_get_port());
	motor_ensure_torque_on();
	motor_move(1, face_positions[0], dur1, 0);
	g_usleep(100000);
	motor_move(2, arm_positions[0], dur2, 0);
	int released = motor_check_arm_stuck_after_9h();
	// 떨림 방지를 위해 9시 이동 완료 후 토크 해제
	g_usleep(dur2 > dur1 ? dur2 * 1000 : dur1 * 1000); // 이동 시간만큼 대기
	motor_disable_torque_all();
	motor_close();
	base_done_data_t *bd = g_new(base_done_data_t, 1);
	bd->released = released;
	bd->op = op;
	g_idle_add(on_base_done_idle, bd);
	return NULL;
}

void on_base_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;
	if (!motor_init(motor_get_port())) {
		GtkWidget *win = gtk_widget_get_toplevel(op->panel);
		if (!GTK_IS_WINDOW(win)) win = NULL;
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
			GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, NULL);
		gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg),
			_("서보모터가 연결되어 있지 않거나 <b>aitimebot</b>이 필요합니다.\n<a href=\"http://mrwatchmaker.com\">mrwatchmaker.com</a> 에서 구매해 주세요."));
		gtk_window_set_title(GTK_WINDOW(dlg), _("장치 연결 오류"));
		if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		return;
	}
	motor_close();
	g_thread_new("base_9h", base_move_thread_func, data);
}

static gboolean auto_measure_tick(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	int total = 4 + op->num_custom; // 고정 4 + 추가된 커스텀 수

	if (op->auto_measure_state == 0) {
		return G_SOURCE_REMOVE;
	}

	op->auto_measure_countdown--;

	if (op->auto_measure_countdown > 0) {
		char buf[64];
		sprintf(buf, _("측정 중... %d 초"), op->auto_measure_countdown);
		gtk_button_set_label(GTK_BUTTON(op->auto_measure_button), buf);
		return G_SOURCE_CONTINUE;
	}

	// 카운트다운 0 → 현재 자세 값 기록
	int idx = op->auto_measure_state - 1;
	if (idx >= 0 && idx < total) {
		char buf[128];
		double rate = op->snst->rate;
		double amp  = op->snst->amp;
		double be   = op->snst->be;
		sprintf(buf, _("Rate: %.1f s/d  |  Amp: %.0f°  |  BE: %.1f ms"), rate, amp, be);
		gtk_label_set_text(GTK_LABEL(op->pos_labels[idx]), buf);
		// 원시값 저장 (진단 리포트용)
		op->pos_rate[idx]     = rate;
		op->pos_amp[idx]      = amp;
		op->pos_be[idx]       = be;
		op->pos_measured[idx] = 1;
	}

	// 다음 자세로 전진
	op->auto_measure_state++;
	if (op->auto_measure_state > total) {
		op->auto_measure_state = 0;
		gtk_button_set_label(GTK_BUTTON(op->auto_measure_button), _("⏎  베이스로 복귀 중..."));
		// 베이스(9시) 자리로 복귀
		int dur1 = calc_duration(last_pos1, face_positions[0]);
		int dur2 = calc_duration(last_pos2, arm_positions[0]);
		motor_init(motor_get_port());
		motor_ensure_torque_on();
		motor_move(1, face_positions[0], dur1, 0);
		g_usleep(100000);
		motor_move(2, arm_positions[0], dur2, 0);
		// 떨림 방지를 위해 복귀 후 토크 끄기
		g_usleep(dur2 > dur1 ? dur2 * 1000 : dur1 * 1000); // 이동 완료 대기
		motor_disable_torque_all();
		last_pos1 = face_positions[0];
		last_pos2 = arm_positions[0];
		motor_close();
		gtk_button_set_label(GTK_BUTTON(op->auto_measure_button), _("▶  자세차 자동 측정"));
		// 측정 완료 → 진단 리포트 생성
		extern void generate_analysis(struct output_panel *op);
		generate_analysis(op);
		return G_SOURCE_REMOVE;
	}

	// 다음 자세 모터 이동
	motor_init(motor_get_port());
	motor_ensure_torque_on();
	{
		int next = op->auto_measure_state - 1;
		int p1, p2;
		if (next >= 4) {
			int c = next - 4;
			p1 = clamp_face(custom_face_pos[c]);
			p2 = clamp_arm(custom_arm_pos[c]);
		} else {
			p1 = clamp_face(face_positions[next]);
			p2 = clamp_arm(arm_positions[next]);
		}
		int t1 = calc_duration(last_pos1, p1);
		int t2 = calc_duration(last_pos2, p2);
		motor_move(1, p1, t1, 0);
		g_usleep(100000);
		motor_move(2, p2, t2, 0);
		last_pos1 = p1;
		last_pos2 = p2;
	}
	motor_close();

	op->auto_measure_countdown = 20;
	return G_SOURCE_CONTINUE;
}

// ── 커스텀 자세 저장/불러오기 ────────────────────────────────────────
#define CUSTOM_POS_FILE "custom_positions.conf"

static void save_custom_positions(int num) {
	FILE *f = fopen(CUSTOM_POS_FILE, "w");
	if (!f) return;
	for (int i = 0; i < num; i++)
		fprintf(f, "%d,%d\n", custom_face_pos[i], custom_arm_pos[i]);
	fclose(f);
}

// 저장 파일에서 불러와 UI에 적용 (init 시 호출)
int load_custom_positions(struct output_panel *op) {
	FILE *f = fopen(CUSTOM_POS_FILE, "r");
	if (!f) return 0;
	int n = 0;
	while (n < 4 && fscanf(f, "%d,%d\n", &custom_face_pos[n], &custom_arm_pos[n]) == 2) {
		custom_face_pos[n] = clamp_face(custom_face_pos[n]);
		custom_arm_pos[n]  = clamp_arm(custom_arm_pos[n]);
		char name_buf[48];
		sprintf(name_buf, _("★ 커스텀 %d  (%d, %d)"), n + 1, custom_face_pos[n], custom_arm_pos[n]);
		gtk_label_set_text(GTK_LABEL(op->custom_name_labels[n]), name_buf);
		gtk_label_set_text(GTK_LABEL(op->pos_labels[4 + n]), _("Rate: -- s/d  |  Amp: --°  |  BE: -- ms"));
		gtk_widget_set_no_show_all(op->custom_rows[n], FALSE);
		gtk_widget_show_all(op->custom_rows[n]);
		n++;
	}
	fclose(f);
	op->num_custom = n;
	return n;
}

// 모든 커스텀 행 레이블 갱신 (삭제 후 재정렬)
static void refresh_custom_rows(struct output_panel *op) {
	for (int i = 0; i < 4; i++) {
		if (i < op->num_custom) {
			char name_buf[48];
			sprintf(name_buf, _("★ 커스텀 %d  (%d, %d)"), i + 1, custom_face_pos[i], custom_arm_pos[i]);
			gtk_label_set_text(GTK_LABEL(op->custom_name_labels[i]), name_buf);
			gtk_label_set_text(GTK_LABEL(op->pos_labels[4 + i]), _("Rate: -- s/d  |  Amp: --°  |  BE: -- ms"));
			gtk_widget_set_no_show_all(op->custom_rows[i], FALSE);
			gtk_widget_show_all(op->custom_rows[i]);
		} else {
			gtk_widget_set_no_show_all(op->custom_rows[i], TRUE);
			gtk_widget_hide(op->custom_rows[i]);
		}
	}
}

void on_delete_custom_clicked(GtkWidget *widget, gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "row_idx"));

	if (idx < 0 || idx >= op->num_custom) return;

	// 해당 슬롯 이후를 앞으로 당기기
	for (int i = idx; i < op->num_custom - 1; i++) {
		custom_face_pos[i] = custom_face_pos[i + 1];
		custom_arm_pos[i]  = custom_arm_pos[i + 1];
	}
	op->num_custom--;

	refresh_custom_rows(op);
	save_custom_positions(op->num_custom);
}

void on_add_custom_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;

	if (op->num_custom >= 4) return; // 최대 4개

	int c = op->num_custom;
	custom_face_pos[c] = clamp_face(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom_id1_spin)));
	custom_arm_pos[c]  = clamp_arm(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(custom_id2_spin)));

	// 이름 레이블 업데이트 및 행 표시
	char name_buf[32];
	sprintf(name_buf, _("★ 커스텀 %d  (%d, %d)"), c + 1, custom_face_pos[c], custom_arm_pos[c]);
	gtk_label_set_text(GTK_LABEL(op->custom_name_labels[c]), name_buf);
	gtk_widget_set_no_show_all(op->custom_rows[c], FALSE); // 플래그 해제 후 표시
	gtk_widget_show_all(op->custom_rows[c]);

	op->num_custom++;
	save_custom_positions(op->num_custom); // 자동 저장
}

void on_auto_measure_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;

	if (op->auto_measure_state != 0) {
		// 취소
		op->auto_measure_state = 0;
		gtk_button_set_label(GTK_BUTTON(op->auto_measure_button), _("▶  자세차 자동 측정"));
		motor_close();
		return;
	}

	// 시작
	if (!motor_init(motor_get_port())) {
		GtkWidget *win = gtk_widget_get_toplevel(op->panel);
		if (!GTK_IS_WINDOW(win)) win = NULL;
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
			GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, NULL);
		gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg),
			_("서보모터가 연결되어 있지 않거나 <b>aitimebot</b>이 필요합니다.\n<a href=\"http://mrwatchmaker.com\">mrwatchmaker.com</a> 에서 구매해 주세요."));
		gtk_window_set_title(GTK_WINDOW(dlg), _("장치 연결 오류"));
		if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		op->auto_measure_state = 0;
		return;
	}
	motor_ensure_torque_on();

	// 레이블 초기화 (고정 4 + 추가된 커스텀)
	int total = 4 + op->num_custom;
	for (int i = 0; i < total; i++) {
		gtk_label_set_text(GTK_LABEL(op->pos_labels[i]), _("Rate: -- s/d  |  Amp: --°  |  BE: -- ms"));
	}

	op->auto_measure_state = 1;
	op->auto_measure_countdown = 20;
	{
		int dur1 = calc_duration(last_pos1, face_positions[0]);
		int dur2 = calc_duration(last_pos2, arm_positions[0]);
		motor_move(1, face_positions[0], dur1, 0);
		g_usleep(100000);
		motor_move(2, arm_positions[0], dur2, 0);
		last_pos1 = face_positions[0];
		last_pos2 = arm_positions[0];
	}
	motor_close();

	g_timeout_add_seconds(1, auto_measure_tick, op);
}

// ── Watch Winder ─────────────────────────────────────────────────────
// face/arm_positions 인덱스: 9시=0, 12시=1, 3시=2, 6시=3
/* 와인더는 밥 주기 용이므로 한 자세에 오래 멈추지 않게,
 * 정지 대기 시간은 0초(또는 최소)로 운용한다. */
#define WINDER_DWELL_SEC 0

static void winder_move_to_step(struct output_panel *op) {
	const WinderPreset *p = &winder_presets[op->winder_preset];
	int total, pos_idx, p1, p2;

	if (op->winder_preset == WINDER_PRESET_POSITIONAL) {
		total = 4 + op->num_custom;
		if (total <= 0) total = 1;
		pos_idx = op->winder_state % total;
		get_positional_slot_face_arm(op, pos_idx, &p1, &p2);
		p1 = clamp_face_hard(p1);
		p2 = clamp_arm_hard(p2);
	} else if (op->winder_preset == WINDER_PRESET_CUSTOM_ONLY) {
		total = op->num_custom;
		if (total <= 0) total = 1;
		pos_idx = op->winder_state % total;
		p1 = clamp_face_hard(custom_face_pos[pos_idx]);
		p2 = clamp_arm_hard(custom_arm_pos[pos_idx]);
	} else {
		total = p->len;
		pos_idx = p->seq[op->winder_state % p->len];
		p1 = clamp_face_hard(face_positions[pos_idx]);
		p2 = clamp_arm_hard(arm_positions[pos_idx]);
	}

	int dur1 = winder_calc_duration(last_pos1, p1);
	int dur2 = winder_calc_duration(last_pos2, p2);

	motor_init(motor_get_port());
	motor_ensure_torque_on();
	motor_move(1, p1, dur1, 0);
	g_usleep(100000);
	motor_move(2, p2, dur2, 0);
	last_pos1 = p1;
	last_pos2 = p2;
	motor_close();

	/* 이 모드에서는 '도착 대기' 자체가 없음 */
	op->winder_countdown = 0;
}

static gboolean winder_tick(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	if (!op->winder_active) return G_SOURCE_REMOVE;

	const WinderPreset *p = &winder_presets[op->winder_preset];
	int total;
	if (op->winder_preset == WINDER_PRESET_POSITIONAL)
		total = 4 + op->num_custom;
	else if (op->winder_preset == WINDER_PRESET_CUSTOM_ONLY)
		total = op->num_custom;
	else
		total = p->len;
	if (total <= 0) total = 1;

	op->winder_state = (op->winder_state + 1) % total;
	if (op->winder_state == 0) op->winder_cycles++;
	winder_move_to_step(op);
	return G_SOURCE_CONTINUE;
}

void on_winder_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;

	if (op->winder_active) {
		op->winder_active = 0;
#ifdef _WIN32
		/* 와인더 정지 시 절전 모드 다시 허용 */
		SetThreadExecutionState(ES_CONTINUOUS);
#endif
		gtk_button_set_label(GTK_BUTTON(op->winder_button), "🔄  와치와인더 시작");
		gtk_style_context_remove_class(gtk_widget_get_style_context(op->winder_button), "btn-winder-stop");
		gtk_style_context_add_class   (gtk_widget_get_style_context(op->winder_button), "btn-winder");
		gtk_label_set_text(GTK_LABEL(op->winder_status_label), _("대기 중 — 프리셋을 선택하고 시작하세요"));
		// 프리셋 콤보 다시 활성화
		gtk_widget_set_sensitive(op->winder_preset_combo, TRUE);
		/* 정지 시 토크 OFF로 버징 방지 */
		if (!op->winder_serial_open) motor_init(motor_get_port());
		motor_disable_torque_all();
		motor_close();
		return;
	}

	// 프리셋 읽기
	op->winder_preset = gtk_combo_box_get_active(GTK_COMBO_BOX(op->winder_preset_combo));
	if (op->winder_preset < 0 || op->winder_preset >= WINDER_PRESET_COUNT)
		op->winder_preset = 0;

	if (op->winder_preset == WINDER_PRESET_CUSTOM_ONLY && op->num_custom <= 0) {
		gtk_label_set_text(GTK_LABEL(op->winder_status_label),
			_("커스텀 위치를 먼저 추가한 뒤 시작하세요."));
		return;
	}

	if (!motor_init(motor_get_port())) {
		GtkWidget *win = gtk_widget_get_toplevel(op->panel);
		if (!GTK_IS_WINDOW(win)) win = NULL;
		GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
			GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, NULL);
		gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg),
			_("서보모터가 연결되어 있지 않거나 <b>aitimebot</b>이 필요합니다.\n<a href=\"http://mrwatchmaker.com\">mrwatchmaker.com</a> 에서 구매해 주세요."));
		gtk_window_set_title(GTK_WINDOW(dlg), _("장치 연결 오류"));
		if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		return;
	}
	motor_close();

	op->winder_active = 1;
	op->winder_state  = 0;
	op->winder_cycles = 0;
	op->winder_countdown = 0;

#ifdef _WIN32
	/* 와인더 사용 중 PC 절전 모드 방지 (와인더가 꺼지지 않도록) */
	SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif

	gtk_button_set_label(GTK_BUTTON(op->winder_button), _("⏹  와치와인더 정지"));
	gtk_style_context_remove_class(gtk_widget_get_style_context(op->winder_button), "btn-winder");
	gtk_style_context_add_class   (gtk_widget_get_style_context(op->winder_button), "btn-winder-stop");
	// 프리셋 콤보 비활성화 (실행 중 변경 방지)
	gtk_widget_set_sensitive(op->winder_preset_combo, FALSE);

	op->winder_tick_ms = WINDER_TICK_SEC * 1000;
	winder_move_to_step(op);
	g_timeout_add_seconds(WINDER_TICK_SEC, winder_tick, op);
}

// ── 진단 리포트 생성 ─────────────────────────────────────────────────
static void atv_append(GtkTextBuffer *buf, GtkTextIter *it,
                       const char *tag, const char *text)
{
	if (tag)
		gtk_text_buffer_insert_with_tags_by_name(buf, it, text, -1, tag, NULL);
	else
		gtk_text_buffer_insert(buf, it, text, -1);
}

void generate_analysis(struct output_panel *op)
{
	int total = 4 + op->num_custom;
	int count = 0;

	double min_rate = 9999, max_rate = -9999;
	double sum_rate = 0, sum_amp = 0, sum_be = 0;
	int    min_idx = 0, max_idx = 0;

	for (int i = 0; i < total && i < 8; i++) {
		if (!op->pos_measured[i]) continue;
		count++;
		sum_rate += op->pos_rate[i];
		sum_amp  += op->pos_amp[i];
		sum_be   += op->pos_be[i];
		if (op->pos_rate[i] < min_rate) { min_rate = op->pos_rate[i]; min_idx = i; }
		if (op->pos_rate[i] > max_rate) { max_rate = op->pos_rate[i]; max_idx = i; }
	}
	if (count == 0) return;

	double pos_error = max_rate - min_rate;
	double avg_amp   = sum_amp  / count;
	double avg_be    = sum_be   / count;
	double avg_rate  = sum_rate / count;

	// 자세차·진폭·비트에러가 전부 0에 가까우면 유효한 측정이 아님 → 등급 미표시
	int no_valid_data = (fabs(pos_error) < 0.01 && fabs(avg_amp) < 0.01 && fabs(avg_be) < 0.01);

	// ── 등급 계산 ─────────────────────────────────────────────────────
	// 자세차 등급 (편차 기준)
	int gp = pos_error <= 5  ? 0 :
	         pos_error <= 10 ? 1 :
	         pos_error <= 20 ? 2 :
	         pos_error <= 30 ? 3 :
	         pos_error <= 45 ? 4 : 5;
	// 진폭 등급
	int ga = avg_amp >= 270 ? 0 :
	         avg_amp >= 240 ? 1 :
	         avg_amp >= 210 ? 2 :
	         avg_amp >= 180 ? 3 :
	         avg_amp >= 150 ? 4 : 5;
	// 비트에러 등급
	int gb = avg_be <= 0.3 ? 0 :
	         avg_be <= 0.5 ? 1 :
	         avg_be <= 0.7 ? 2 :
	         avg_be <= 1.0 ? 3 :
	         avg_be <= 1.5 ? 4 : 5;
	// 종합 등급 (가중평균: 자세차 40%, 진폭 40%, BE 20%)
	int go = (gp * 4 + ga * 4 + gb * 2) / 10;

	const char *gname[]  = {"S", "A", "B", "C", "D", "E"};
	const char *gtag[]   = {"tag_s","tag_a","tag_b","tag_c","tag_d","tag_e"};
	const char *gstar[]  = {"★★★★★","★★★★☆","★★★☆☆","★★☆☆☆","★☆☆☆☆","☆☆☆☆☆"};
	const char *pos_names[] = {_("9시 (기본)"), _("12시"), _("3시"), _("6시"),
	                            _("커스텀 1"), _("커스텀 2"), _("커스텀 3"), _("커스텀 4")};

	// ── 텍스트 버퍼 구성 ──────────────────────────────────────────────
	GtkTextBuffer *buf = gtk_text_view_get_buffer(
	                         GTK_TEXT_VIEW(op->analysis_textview));
	gtk_text_buffer_set_text(buf, "", -1);

	// 태그 생성 (한 번만 등록되도록 확인)
	GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(buf);
	struct { const char *name; const char *fg; int bold; double size; } tags[] = {
		{"tag_title",  "#c8a840", 1, 14},
		{"tag_head",   "#4fc3f7", 1, 13},
		{"tag_silver", "#8090aa", 0, 13},
		{"tag_bright", "#e8f0ff", 1, 13},
		{"tag_s",      "#ffd700", 1, 13},
		{"tag_a",      "#4fc3f7", 1, 13},
		{"tag_b",      "#66bb6a", 1, 13},
		{"tag_c",      "#ffa726", 1, 13},
		{"tag_d",      "#ef5350", 1, 13},
		{"tag_e",      "#b71c1c", 1, 13},
		{"tag_good",   "#66bb6a", 0, 13},
		{"tag_warn",   "#ffa726", 0, 13},
		{"tag_bad",    "#ef5350", 0, 13},
		{"tag_pos",    "#90caf9", 0, 13},
		{"tag_sep",    "#2a3a5a", 0, 13},
	};
	for (int i = 0; i < (int)(sizeof(tags)/sizeof(tags[0])); i++) {
		if (!gtk_text_tag_table_lookup(tt, tags[i].name)) {
			GtkTextTag *t = gtk_text_buffer_create_tag(buf, tags[i].name,
				"foreground", tags[i].fg,
				"weight",     tags[i].bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
				"size-points", (double)tags[i].size,
				NULL);
			(void)t;
		}
	}

	GtkTextIter it;
	gtk_text_buffer_get_start_iter(buf, &it);
	char tmp[256];

	// ══════════════════════════════════════════════════════════════════
	char *title_t = g_strdup_printf(
		"╔═══════════════════════════════════════════╗\n"
		"%s"
		"╚═══════════════════════════════════════════╝\n\n",
		_("║    🕰  MrWatchmaker  자세차 진단 리포트    ║\n"));
	atv_append(buf,&it,"tag_title", title_t);
	g_free(title_t);

	// 종합 등급 (미측정 시 등급 생략)
	if (no_valid_data) {
		atv_append(buf,&it,"tag_silver",
			_("  — 종합 등급: ( 미측정 — 자세별 측정 후 표시됩니다 )\n\n"));
	} else {
		{ char *ts = g_strdup_printf(_("  %s 종합 등급: "), gstar[go]); atv_append(buf,&it,"tag_silver", ts); g_free(ts); }
		{ char *ts = g_strdup_printf(_("[ %s급 ]"), gname[go]); atv_append(buf,&it, gtag[go], ts); g_free(ts); }
		atv_append(buf,&it,"tag_silver","  ( ");
		const char *overall_desc[] = {
			_("마스터피스 수준 — 탁월합니다"),
			_("매우 우수 — 컬렉터 등급"),
			_("양호 — 일상 사용에 충분"),
			_("보통 — 점검을 고려하세요"),
			_("주의 — 전문 점검 권장"),
			_("정비 필요 — 즉시 오버홀 권장")
		};
		atv_append(buf,&it, gtag[go], overall_desc[go]);
		atv_append(buf,&it,"tag_silver"," )\n\n");
	}

	// 요약 수치
	atv_append(buf,&it,"tag_head","  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
	{ char *ts = g_strdup_printf(_("  ⏱  자세차(최대편차)   :  %.1f s/d\n"), pos_error); atv_append(buf,&it, gtag[gp], ts); g_free(ts); }
	{ char *ts = g_strdup_printf(_("  ⚡  평균 진폭          :  %.0f°\n"), avg_amp); atv_append(buf,&it, gtag[ga], ts); g_free(ts); }
	{ char *ts = g_strdup_printf(_("  💓  평균 비트에러      :  %.2f ms\n"), avg_be); atv_append(buf,&it, gtag[gb], ts); g_free(ts); }
	{ char *ts = g_strdup_printf(_("  📌  평균 레이트        :  %+.1f s/d\n"), avg_rate); atv_append(buf,&it,"tag_bright", ts); g_free(ts); }
	atv_append(buf,&it,"tag_head","  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

	// 자세별 결과 표
	atv_append(buf,&it,"tag_title",_("  📋  자세별 측정 결과\n"));
	atv_append(buf,&it,"tag_sep",
		"  ─────────────────────────────────────────\n");
	atv_append(buf,&it,"tag_silver",
		_("  자세         레이트      진폭    비트에러\n"));
	atv_append(buf,&it,"tag_sep",
		"  ─────────────────────────────────────────\n");
	for (int i = 0; i < total && i < 8; i++) {
		if (!op->pos_measured[i]) continue;
		{ char *ts = g_strdup_printf("  %-11s  %+6.1f s/d  %4.0f°   %.2f ms\n", pos_names[i], op->pos_rate[i], op->pos_amp[i], op->pos_be[i]); atv_append(buf,&it,"tag_pos", ts); g_free(ts); }
	}
	atv_append(buf,&it,"tag_sep",
		"  ─────────────────────────────────────────\n\n");

	if (no_valid_data) {
		atv_append(buf,&it,"tag_silver",
			_("  측정 데이터가 없어 상세 분석을 표시할 수 없습니다.\n  자세별 측정을 완료하면 등급과 분석이 표시됩니다.\n"));
	} else {
	// ── 자세차 상세 분석 ──────────────────────────────────────────────
	atv_append(buf,&it,"tag_title",_("  🔍  자세차 분석\n"));
	{ char *ts = g_strdup_printf(_("  가장 느린 자세 : %s (%.1f s/d)\n"), pos_names[min_idx], min_rate); atv_append(buf,&it,"tag_silver", ts); g_free(ts); }
	{ char *ts = g_strdup_printf(_("  가장 빠른 자세 : %s (%.1f s/d)\n\n"), pos_names[max_idx], max_rate); atv_append(buf,&it,"tag_silver", ts); g_free(ts); }

	const char *pos_comment[] = {
		_("  ✅ 자세 간 편차 5 s/d 이하 — 환상적입니다.\n     밸런스 휠의 동적 균형이 완벽에 가까운\n     수준입니다. 최상급 무브먼트 조율 상태.\n"),
		_("  ✅ 자세 간 편차 10 s/d 이하 — 매우 훌륭합니다.\n     중력에 의한 오차가 잘 억제되어 있으며,\n     일상 착용 시 정밀한 시간을 유지합니다.\n"),
		_("  🟡 자세 간 편차 20 s/d 이하 — 양호한 상태.\n     소폭의 윤활 열화나 피봇 마모가 있을 수\n     있습니다. 4~5년 주기 점검을 권장합니다.\n"),
		_("  🟠 자세 간 편차 30 s/d 이하 — 보통 수준.\n     무브먼트 내 오일 경화 또는 밸런스 스프링\n     변형 가능성이 있습니다. 점검을 권장합니다.\n"),
		_("  ⚠️  자세 간 편차 45 s/d 이하 — 점검이 필요합니다.\n     피봇 마모 또는 보석(jewel) 손상 가능성이\n     있습니다. 가까운 시계사에 방문 권장.\n"),
		_("  🔴 자세 간 편차 45 s/d 초과 — 즉시 정비가 필요합니다.\n     무브먼트 내부에 심각한 마모나 손상이\n     의심됩니다. 신뢰할 수 있는 시계사에서\n     오버홀을 받으세요.\n"),
	};
	atv_append(buf,&it, gtag[gp], pos_comment[gp]);

	// ── 진폭 분석 ─────────────────────────────────────────────────────
	atv_append(buf,&it,"tag_title",_("\n  ⚡  진폭(에너지) 분석\n"));
	const char *amp_comment[] = {
		_("  ✅ 270° 이상 — 파워가 넘칩니다.\n     메인스프링의 탄성이 완벽하게 유지되어\n     있으며, 오일 상태도 양호합니다.\n"),
		_("  ✅ 240~270° — 이상적인 진폭 범위입니다.\n     무브먼트에 충분한 에너지가 공급되고 있어\n     안정적인 조속 기능을 기대할 수 있습니다.\n"),
		_("  🟡 210~240° — 조금 낮은 편입니다.\n     오일이 점차 굳어가거나 메인스프링\n     파워리저브가 줄었을 수 있습니다.\n     3~5년 주기 오버홀 시점을 고려하세요.\n"),
		_("  🟠 180~210° — 진폭이 다소 낮습니다.\n     오일 경화로 인한 마찰 증가가 의심됩니다.\n     오버홀을 받으면 진폭이 회복될 수 있습니다.\n"),
		_("  ⚠️  150~180° — 진폭이 매우 낮습니다.\n     무브먼트에 에너지가 충분히 전달되지 않고\n     있습니다. 조속 기능이 불안정해질 수 있어\n     빠른 정비를 권장합니다.\n"),
		_("  🔴 150° 미만 — 위험 수준의 낮은 진폭.\n     피봇 고착 또는 오일 완전 경화 상태가\n     의심됩니다. 즉각적인 오버홀이 필요합니다.\n"),
	};
	atv_append(buf,&it, gtag[ga], amp_comment[ga]);

	// ── 비트에러 분석 ─────────────────────────────────────────────────
	atv_append(buf,&it,"tag_title",_("\n  💓  비트에러 분석\n"));
	const char *be_comment[] = {
		_("  ✅ 0.3 ms 이하 — 완벽한 균형 상태.\n     팔레트 포크(Pallet Fork)가 이상적인\n     위치에 세팅되어 있습니다.\n"),
		_("  ✅ 0.3~0.5 ms — 매우 우수합니다.\n     틱(Tick)과 톡(Tock)의 간격이 균등하여\n     안정적인 조속 기능을 보장합니다.\n"),
		_("  🟡 0.5~0.7 ms — 허용 범위 내입니다.\n     팔레트 포크의 미세 조정으로 개선이\n     가능합니다. 정기 점검 시 조정 권장.\n"),
		_("  🟠 0.7~1.0 ms — 조정이 필요합니다.\n     비대칭적인 진동으로 인해 자세에 따라\n     레이트 변동이 커질 수 있습니다.\n"),
		_("  ⚠️  1.0~1.5 ms — 팔레트 포크 조정이 필요합니다.\n     충격이나 낙하로 인해 팔레트 포크가\n     어긋났을 가능성이 있습니다.\n"),
		_("  🔴 1.5 ms 초과 — 즉각적인 조정이 필요합니다.\n     비트에러가 이 수준이면 자세에 따라\n     시간 오차가 크게 달라집니다.\n"),
	};
	atv_append(buf,&it, gtag[gb], be_comment[gb]);

	// ── 전문가 의견 ──────────────────────────────────────────────────
	atv_append(buf,&it,"tag_head",
		"\n  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
	atv_append(buf,&it,"tag_title",_("  💡  전문가 종합 소견\n"));
	atv_append(buf,&it,"tag_head",
		"  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

	// 평균 레이트 해석
	if (avg_rate > 5)
		atv_append(buf,&it,"tag_warn",
			_("  시계가 하루 평균 빠르게 가고 있습니다.\n  헤어스프링 장력이 강하거나 레귤레이터가\n  \"F(Fast)\" 방향으로 치우쳐 있을 수 있습니다.\n"));
	else if (avg_rate < -10)
		atv_append(buf,&it,"tag_warn",
			_("  시계가 하루 평균 느리게 가고 있습니다.\n  오일 경화로 인한 마찰 또는 레귤레이터가\n  \"S(Slow)\" 방향으로 치우쳐 있을 수 있습니다.\n"));
	else
		atv_append(buf,&it,"tag_good",
			_("  평균 레이트가 우수한 범위에 있습니다.\n  시계의 전반적인 조율 상태가 양호합니다.\n"));

	// 종합 권고사항
	const char *final_advice[] = {
		_("\n  이 시계는 거의 완벽에 가까운 상태입니다.\n  현재 상태를 유지하기 위해 3~5년마다\n  예방적 오버홀을 권장합니다. 🏆\n"),
		_("\n  이 시계는 매우 우수한 상태입니다.\n  정기적인 오버홀 주기를 지키면 이 상태를\n  오래 유지할 수 있습니다. ✨\n"),
		_("\n  전반적으로 양호한 상태입니다.\n  진폭 또는 자세차 중 한 항목이 주의 수준으로,\n  4년 이내 오버홀을 권장합니다.\n"),
		_("\n  시계가 주의가 필요한 상태입니다.\n  오버홀을 통해 오일 교체 및 부품 점검을\n  받으시면 상당한 개선이 기대됩니다.\n"),
		_("\n  시계의 상태가 좋지 않습니다.\n  신뢰할 수 있는 시계사에서 오버홀을 받으세요.\n  방치 시 부품 마모가 가속화될 수 있습니다. ⚠️\n"),
		_("\n  즉각적인 오버홀이 필요합니다.\n  무브먼트 내부에 심각한 문제가 의심됩니다.\n  전문 시계사 방문을 강력히 권고합니다. 🔴\n"),
	};
	atv_append(buf,&it, gtag[go], final_advice[go]);
	}

	atv_append(buf,&it,"tag_sep",
		"\n  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
	atv_append(buf,&it,"tag_silver",
		_("  MrWatchmaker — 당신의 소중한 타임피스를 위해\n\n"));

	// 표시
	gtk_widget_set_no_show_all(op->analysis_scroll, FALSE);
	gtk_widget_show_all(op->analysis_scroll);

	// 스크롤을 맨 위로
	GtkTextIter top;
	gtk_text_buffer_get_start_iter(buf, &top);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(op->analysis_textview),
	                             &top, 0.0, TRUE, 0.0, 0.0);
}
