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
#include <stdio.h>
#include <stdlib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* 자세차: 고정 6(5·6번 이름=custom_labels ch/cb) → 슬롯 0..5 */
#define POS_FIXED_COUNT 6
/* 자세차 자동측정: Present(논리틱)와 🕘9시(기본) 목표틱 차이 허용 한도 — 초과 시 기준점 안내 */
#define POS_AUTO_MEASURE_BASELINE_MAX_DELTA_TICKS 100

// ── Watch Winder 프리셋 (init_output_panel에서 사용하므로 파일 최상단) ──
/* 고정 5·6번(윗면/아랫면 자리) 표시명 — mrwatchmaker_coords.txt custom_labels (기본 ch / cb) */
static char g_custom_label[2][40] = { "ch", "cb" };

typedef struct { const char *name; const int *seq; int len; } WinderPreset;

static const int wp0[] = {3,0,1,2,3,2,1,0};
static const int wp1[] = {0,1,2,3};
static const int wp2[] = {0,3,2,1};
static const int wp3[] = {0,2};
static const int wp4[] = {1,3};
static const int wp5[] = {0,3,0,3};
static const int wp6[] = {3,0,1,0};
static const int wp7[] = {0,1,2,3,2,1};

static const int wp_pos[] = {0,1,2,3,4,5}; /* 고정 6자세 순서 */

static const WinderPreset winder_presets[] = {
	{"풀 코스 (6→9→12→3→6→3→12→9)",     wp0, 8},
	{"시계방향 회전 (9→12→3→6)",          wp1, 4},
	{"반시계방향 회전 (9→6→3→12)",        wp2, 4},
	{"좌우 스윙 (9시 ↔ 3시)",             wp3, 2},
	{"상하 스윙 (12시 ↔ 6시)",            wp4, 2},
	{"가볍게 흔들기 (9시 ↔ 6시)",         wp5, 4},
	{"6→9→12→9 왕복",                    wp6, 4},
	{"시계방향 왕복 (9→12→3→6→3→12→9)", wp7, 6},
	{"자세차 자동측정 순서 (반복)",       wp_pos, 6},
};
#define WINDER_PRESET_COUNT 9
#define WINDER_PRESET_POSITIONAL 8

/* ── STS3215 소프트웨어 안전 범위: 기본값은 아래, 실제는 mrwatchmaker_coords.txt ─ */
#define DEFAULT_FACE_MIN 0
#define DEFAULT_FACE_MAX 4087
#define DEFAULT_ARM_MIN  125
#define DEFAULT_ARM_MAX  2071

#define MRW_COORDS_FILE "mrwatchmaker_coords.txt"
#define REF_9OCLOCK_IMAGE_FILE "reference_9oclock.png"
#define REF9_PARENT_KEY "ref9-parent-window"

static int g_face_min = DEFAULT_FACE_MIN;
static int g_face_max = DEFAULT_FACE_MAX;
static int g_arm_min = DEFAULT_ARM_MIN;
static int g_arm_max = DEFAULT_ARM_MAX;
/* 커스텀 스핀(ID1 Face / ID2 Arm) 기본값 — 파일 spin 줄에서 덮어씀 */
static int g_spin_face = 2992;
static int g_spin_arm = 108;

/* 고정 6자세: 9·12·3·6시 + 윗면·아랫면 — 파일에서 6줄(face arm) */
/* 6시 Face: 12시(2997)과 동일 틱이면 혼동 → 다이얼 180° norm(2997+2048)=949 */
static int face_positions[6] = {1975, 2997, 4010, 949, 1975, 2997};
static int arm_positions[6]  = {1079, 2070, 1081, 125, 1079, 2070};
static int last_pos1 = 1975;
static int last_pos2 = 1079;

/* Android MainActivity: microAdjBaseline — ± 후 [보정 적용] 시 tickDelta 누적에 사용 */
static int micro_adj_base_face = 0;
static int micro_adj_base_arm = 0;
static int micro_adj_base_valid = 0;

#define MICRO_STEP_FACE 50
#define MICRO_STEP_ARM  12
/* 미세조정 전용: calc_duration()은 최소 5초라 버튼 반응이 너무 느림 → 짧은 고정 이동 시간 */
#define MICRO_NUDGE_MOVE_MS 420
#define MICRO_HOLD_GAP_US   75000U

enum {
	MICRO_REP_NONE = 0,
	MICRO_REP_FACE_M,
	MICRO_REP_FACE_P,
	MICRO_REP_ARM_M,
	MICRO_REP_ARM_P
};

static int norm4096_pc(int p) {
	p %= 4096;
	if (p < 0) p += 4096;
	return p;
}

static int logical_face_from_raw(int raw, int vd) {
	return norm4096_pc(raw - vd);
}
static int logical_arm_from_raw(int raw, int vd) {
	return norm4096_pc(raw - vd);
}

static int clamp_face_hard(int v);
static int clamp_arm_hard(int v);

/* 논리틱 원호(4096)에서 base → cur 최단 부호 거리 (Android tickDeltaSigned 와 동일) */
static int tick_delta_signed(int base, int cur) {
	int b = norm4096_pc(base);
	int t = norm4096_pc(cur);
	int cw = (t >= b) ? (t - b) : (t - b + 4096);
	if (cw > 2048) cw -= 4096;
	return cw;
}

/* 9시 기준 고정 규칙:
 * - 9/12/3/6: Arm 고정, Face는 +1023씩 회전
 * - ch: 6시에서 Arm만 +2046
 * - cb: ch Arm 고정, Face만 +2046 */
static void rebuild_fixed_positions_from_base(void) {
	int face9 = clamp_face_hard(norm4096_pc(face_positions[0]));
	int arm9 = clamp_arm_hard(norm4096_pc(arm_positions[0]));
	int face6 = norm4096_pc(face9 + (3 * 1023));
	int face12 = norm4096_pc(face9 + 1023);
	int arm6 = arm9;
	int arm_ch = norm4096_pc(arm6 - 1023);

	face_positions[0] = clamp_face_hard(face9);
	arm_positions[0] = clamp_arm_hard(arm9);

	/* 12시/6시 페이스 번호가 바뀌지 않도록 순서를 고정 */
	face_positions[1] = clamp_face_hard(face6);
	arm_positions[1] = clamp_arm_hard(arm9);

	face_positions[2] = clamp_face_hard(norm4096_pc(face9 + 2046));
	arm_positions[2] = clamp_arm_hard(arm9);

	face_positions[3] = clamp_face_hard(face12);
	arm_positions[3] = clamp_arm_hard(arm6);

	/* UI 인덱스 기준으로 ch/cb가 뒤바뀌지 않게 순서를 고정 */
	face_positions[4] = clamp_face_hard(norm4096_pc(face6 + 2046)); /* ch */
	arm_positions[4] = clamp_arm_hard(arm_ch);

	face_positions[5] = clamp_face_hard(face6); /* cb */
	arm_positions[5] = clamp_arm_hard(arm_ch);
}

static void load_coords_from_file(void);
static void save_coords_to_file(void);
static void refresh_pos_coord_labels(struct output_panel *op);
static void update_micro_motor_readout_label_ui(struct output_panel *op);
static gboolean refresh_micro_motor_label_idle(gpointer data);
static gboolean micro_motor_live_label_idle(gpointer data);
static void micro_motor_schedule_live_readout(int face_log, int arm_log, struct output_panel *op);
static gpointer sync_last_pos_from_servos_thread(gpointer data);
static gboolean vis_cal_err_idle(gpointer data);
static gboolean vis_cal_err_read_idle(gpointer data);

/* 여러 워커 스레드가 동시에 실패하면 g_idle_add가 중복되어 같은 오류 창이 연속 표시됨 → idle 1개로 통합 */
static GMutex g_servo_err_sched_mutex;
static guint g_idle_servo_conn_err = 0;
static guint g_idle_servo_read_err = 0;
static gsize g_servo_err_mutex_once = 0;

static void servo_err_mutex_ensure(void)
{
	if (g_once_init_enter(&g_servo_err_mutex_once)) {
		g_mutex_init(&g_servo_err_sched_mutex);
		g_once_init_leave(&g_servo_err_mutex_once, 1);
	}
}

static void schedule_servo_connect_error_dialog_once(struct output_panel *op)
{
	servo_err_mutex_ensure();
	g_mutex_lock(&g_servo_err_sched_mutex);
	if (g_idle_servo_conn_err != 0) {
		g_source_remove(g_idle_servo_conn_err);
		g_idle_servo_conn_err = 0;
	}
	g_idle_servo_conn_err = g_idle_add(vis_cal_err_idle, op);
	g_mutex_unlock(&g_servo_err_sched_mutex);
}

static void schedule_servo_read_error_dialog_once(struct output_panel *op)
{
	servo_err_mutex_ensure();
	g_mutex_lock(&g_servo_err_sched_mutex);
	if (g_idle_servo_read_err != 0) {
		g_source_remove(g_idle_servo_read_err);
		g_idle_servo_read_err = 0;
	}
	g_idle_servo_read_err = g_idle_add(vis_cal_err_read_idle, op);
	g_mutex_unlock(&g_servo_err_sched_mutex);
}

typedef struct {
	struct output_panel *op;
	int kind;
} micro_btn_ud_t;
static gboolean on_micro_button_press(GtkWidget *w, GdkEventButton *ev, gpointer data);
static gboolean on_micro_button_release(GtkWidget *w, GdkEventButton *ev, gpointer data);
void on_batch_apply_clicked(GtkWidget *w, gpointer d);
static GtkWidget *s_ref9_window;
/* 암 풀기 후 손으로 돌릴 때 기준점 미세조정 실시간 반영 (워커 스레드에서 Present 읽기 → 메인 idle로 라벨 갱신) */
static GThread *s_arm_release_poll_thread;
static volatile gint s_arm_release_poll_run;
static struct output_panel *s_arm_release_poll_op;
static void arm_release_live_poll_stop(void);
static void arm_release_live_poll_start(struct output_panel *op);
static gchar *reference_9oclock_image_path(void);
static void show_reference_9oclock_window(struct output_panel *op);
static gboolean idle_show_ref9_on_micro_press(gpointer data);

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
	if (s_arm_release_poll_op == op)
		arm_release_live_poll_stop();
	snapshot_destroy(op->snst);
	free(op);
}

struct output_panel *init_output_panel(struct computer *comp, struct snapshot *snst, int border)
{
	struct output_panel *op = malloc(sizeof(struct output_panel));

	load_coords_from_file();

	op->auto_measure_state = 0;
	op->auto_measure_countdown = 0;
	op->auto_measure_timer = 0;
	op->manual_measure_target = -1;
	op->manual_measure_countdown = 0;
	op->manual_measure_timer = 0;
	for (int i = 0; i < POS_FIXED_COUNT; i++) {
		op->manual_measure_buttons[i] = NULL;
		op->pos_coord_labels[i] = NULL;
	}
	op->micro_motor_readout_label = NULL;
	op->winder_active = 0;
	op->winder_state = 0;
	op->winder_countdown = 0;
	op->winder_cycles = 0;
	op->winder_timeout_id = 0;

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

	// 고정 6자세 행 (5·6번 라벨은 custom_labels 의 ch / cb)
	const char *pos_names_fixed4[] = {
		_("🕘 9시 (기본)"), _("🕛 12시"), _("🕒 3시"), _("🕕 6시"),
	};
	for (int i = 0; i < POS_FIXED_COUNT; i++) {
		GtkWidget *lbl_name;
		if (i < 4) {
			lbl_name = gtk_label_new(pos_names_fixed4[i]);
		} else {
			char *t = g_strdup_printf(i == 4 ? _("⬆ %s") : _("⬇ %s"), g_custom_label[i - 4]);
			lbl_name = gtk_label_new(t);
			g_free(t);
		}
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl_name), "pos-name");
		gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);

		op->pos_labels[i] = gtk_label_new(_("Rate: -- s/d  |  Amp: --°  |  BE: -- ms"));
		gtk_style_context_add_class(gtk_widget_get_style_context(op->pos_labels[i]), "pos-value");
		gtk_widget_set_halign(op->pos_labels[i], GTK_ALIGN_START);

		char coord_buf[96];
		snprintf(coord_buf, sizeof(coord_buf), _("Face %d  │  Arm %d"), face_positions[i], arm_positions[i]);
		op->pos_coord_labels[i] = gtk_label_new(coord_buf);
		gtk_style_context_add_class(gtk_widget_get_style_context(op->pos_coord_labels[i]), "pos-value");
		gtk_widget_set_halign(op->pos_coord_labels[i], GTK_ALIGN_START);
		gtk_widget_set_tooltip_text(op->pos_coord_labels[i],
			_("이 자세로 이동할 때 사용하는 목표 틱(Face / Arm). mrwatchmaker_coords.txt·[기준점 일괄적용]으로 반영합니다."));

		// 고정 자세 측정 버튼 (커스텀과 동일하게 행 오른쪽에 배치)
		GtkWidget *measure_btn = gtk_button_new_with_label(_("측정"));
		gtk_style_context_add_class(gtk_widget_get_style_context(measure_btn), "btn-test");
		g_object_set_data(G_OBJECT(measure_btn), "manual_target", GINT_TO_POINTER(i)); // 0..5 고정
		extern void on_manual_measure_clicked(GtkWidget *widget, gpointer data);
		g_signal_connect(measure_btn, "clicked", G_CALLBACK(on_manual_measure_clicked), op);
		op->manual_measure_buttons[i] = measure_btn;

		/* [이름] [측정값] [자세차 목표틱] [측정] */
		gtk_grid_attach(GTK_GRID(pos_grid), lbl_name,               0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(pos_grid), op->pos_labels[i],      1, i, 1, 1);
		gtk_grid_attach(GTK_GRID(pos_grid), op->pos_coord_labels[i], 2, i, 1, 1);
		gtk_grid_attach(GTK_GRID(pos_grid), measure_btn,            3, i, 1, 1);
	}

	// 버튼 행
	GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(pos_vbox), btn_box, FALSE, FALSE, 4);

	op->auto_measure_button = gtk_button_new_with_label(_("▶  자세차 자동 측정"));
	gtk_style_context_add_class(gtk_widget_get_style_context(op->auto_measure_button), "btn-auto");
	gtk_box_pack_start(GTK_BOX(btn_box), op->auto_measure_button, TRUE, TRUE, 0);
	extern void on_auto_measure_clicked(GtkWidget *widget, gpointer data);
	g_signal_connect(op->auto_measure_button, "clicked", G_CALLBACK(on_auto_measure_clicked), op);

	GtkWidget *calib_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(pos_vbox), calib_row, FALSE, FALSE, 4);
	GtkWidget *btn_arm_rel = gtk_button_new_with_label(_("🔓 암풀기 (충격으로 걸림, 떨어뜨림)"));
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_arm_rel), "btn-test");
	gtk_box_pack_start(GTK_BOX(calib_row), btn_arm_rel, TRUE, TRUE, 0);
	extern void on_arm_release_jam_clicked(GtkWidget *widget, gpointer data);
	g_signal_connect(btn_arm_rel, "clicked", G_CALLBACK(on_arm_release_jam_clicked), op);

	/* Face/Arm ± — 서보 목표 미세 이동 (시각 보정 저장은 [보정] 슬롯 다이얼로그 등에서 사용) */
	GtkWidget *micro_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(pos_vbox), micro_row, FALSE, FALSE, 4);
	GtkWidget *lbl_micro = gtk_label_new(_("기준점 미세조정:"));
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_micro), "spin-label");
	gtk_box_pack_start(GTK_BOX(micro_row), lbl_micro, FALSE, FALSE, 0);
	op->micro_motor_readout_label = gtk_label_new(_("모터 읽는 중…"));
	gtk_style_context_add_class(gtk_widget_get_style_context(op->micro_motor_readout_label), "pos-value");
	gtk_widget_set_halign(op->micro_motor_readout_label, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(micro_row), op->micro_motor_readout_label, FALSE, FALSE, 0);
	GtkWidget *bfm = gtk_button_new_with_label(_("Face −"));
	GtkWidget *bfp = gtk_button_new_with_label(_("Face +"));
	GtkWidget *bam = gtk_button_new_with_label(_("Arm −"));
	GtkWidget *bap = gtk_button_new_with_label(_("Arm +"));
	gtk_style_context_add_class(gtk_widget_get_style_context(bfm), "btn-test");
	gtk_style_context_add_class(gtk_widget_get_style_context(bfp), "btn-test");
	gtk_style_context_add_class(gtk_widget_get_style_context(bam), "btn-test");
	gtk_style_context_add_class(gtk_widget_get_style_context(bap), "btn-test");
	gtk_box_pack_start(GTK_BOX(micro_row), bfm, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(micro_row), bfp, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(micro_row), bam, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(micro_row), bap, FALSE, FALSE, 0);
	{
		static micro_btn_ud_t s_micro_ud[4];
		s_micro_ud[0].op = op; s_micro_ud[0].kind = MICRO_REP_FACE_M;
		s_micro_ud[1].op = op; s_micro_ud[1].kind = MICRO_REP_FACE_P;
		s_micro_ud[2].op = op; s_micro_ud[2].kind = MICRO_REP_ARM_M;
		s_micro_ud[3].op = op; s_micro_ud[3].kind = MICRO_REP_ARM_P;
		gtk_widget_add_events(bfm, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
		gtk_widget_add_events(bfp, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
		gtk_widget_add_events(bam, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
		gtk_widget_add_events(bap, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
		g_signal_connect(bfm, "button-press-event", G_CALLBACK(on_micro_button_press), &s_micro_ud[0]);
		g_signal_connect(bfp, "button-press-event", G_CALLBACK(on_micro_button_press), &s_micro_ud[1]);
		g_signal_connect(bam, "button-press-event", G_CALLBACK(on_micro_button_press), &s_micro_ud[2]);
		g_signal_connect(bap, "button-press-event", G_CALLBACK(on_micro_button_press), &s_micro_ud[3]);
		g_signal_connect(bfm, "button-release-event", G_CALLBACK(on_micro_button_release), NULL);
		g_signal_connect(bfp, "button-release-event", G_CALLBACK(on_micro_button_release), NULL);
		g_signal_connect(bam, "button-release-event", G_CALLBACK(on_micro_button_release), NULL);
		g_signal_connect(bap, "button-release-event", G_CALLBACK(on_micro_button_release), NULL);
	}
	GtkWidget *btn_batch = gtk_button_new_with_label(_("기준점 일괄적용"));
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_batch), "btn-test");
	gtk_widget_set_tooltip_text(btn_batch,
		_("Face/Arm ±로 맞춘 만큼 6자세 목표 틱에 한 번에 반영합니다. 🕘 9시(기본) 자세에 맞춘 뒤 [기준점 일괄적용] 하세요."));
	gtk_box_pack_start(GTK_BOX(micro_row), btn_batch, FALSE, FALSE, 0);
	g_signal_connect(btn_batch, "clicked", G_CALLBACK(on_batch_apply_clicked), op);

	// ── Watch Winder UI (hrow_top 오른쪽, 흰 공간 채움) ─────────────────
	{
		GtkWidget *winder_frame = gtk_frame_new(_("🔄  와치와인더"));
		gtk_style_context_add_class(gtk_widget_get_style_context(winder_frame), "pos-frame");

		// Positional Error 영역을 스크롤 가능하게
		GtkWidget *pos_scroll = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pos_scroll),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		/* 고정 6자세 + 버튼 */
		gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(pos_scroll), 520);
		gtk_widget_set_size_request(pos_scroll, 520, -1);
		gtk_container_add(GTK_CONTAINER(pos_scroll), pos_frame);

		// hrow_top: pos_scroll(왼쪽) + winder_frame(오른쪽)
		GtkWidget *hrow_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		// 작은 해상도에서도 자세차 영역이 너무 작아지지 않도록
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
		for (int i = 0; i < WINDER_PRESET_COUNT; i++) {
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(op->winder_preset_combo),
				_(winder_presets[i].name));
		}
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

	g_thread_new("sync_servo_pose", sync_last_pos_from_servos_thread, op);

	return op;
}

// Auto measure state machine
// Face motor (ID 1) positions: 9, 12, 3, 6, Custom
// 고정 자세 좌표·제한·스핀 기본값은 mrwatchmaker_coords.txt (exe 폴더) 참고

static int clamp_face_hard(int v) {
	if (v < g_face_min) return g_face_min;
	if (v > g_face_max) return g_face_max;
	return v;
}
static int clamp_arm_hard(int v) {
	if (v < g_arm_min) return g_arm_min;
	if (v > g_arm_max) return g_arm_max;
	return v;
}

static int clamp_face_soft(int v) {
	const int minv = g_face_min + FACE_SOFT_MARGIN;
	const int maxv = g_face_max - FACE_SOFT_MARGIN;
	if (v < minv) return minv;
	if (v > maxv) return maxv;
	return v;
}
static int clamp_arm_soft(int v) {
	const int minv = g_arm_min + ARM_SOFT_MARGIN;
	const int maxv = g_arm_max - ARM_SOFT_MARGIN;
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

static void coords_build_path(char *out, size_t outsz)
{
#ifdef _WIN32
	char exe[520];
	DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
	if (n > 0 && n < sizeof(exe)) {
		char *slash = strrchr(exe, '\\');
		if (slash) {
			slash[1] = '\0';
			snprintf(out, outsz, "%s%s", exe, MRW_COORDS_FILE);
			return;
		}
	}
#endif
	snprintf(out, outsz, "%s", MRW_COORDS_FILE);
}

static void coords_trim_line(char *s)
{
	char *a = s;
	while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n')
		a++;
	if (a != s)
		memmove(s, a, strlen(a) + 1);
	size_t L = strlen(s);
	while (L > 0 && (s[L - 1] == ' ' || s[L - 1] == '\t' || s[L - 1] == '\r' || s[L - 1] == '\n'))
		s[--L] = '\0';
}

/* exe 옆 mrwatchmaker_coords.txt: face_lim/arm_lim, custom_labels(고정5·6번=ch/cb), spin, 고정6줄, 커스텀2줄 */
static void load_coords_from_file(void)
{
	char path[600];
	coords_build_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	strncpy(g_custom_label[0], "ch", sizeof(g_custom_label[0]));
	g_custom_label[0][sizeof(g_custom_label[0]) - 1] = '\0';
	strncpy(g_custom_label[1], "cb", sizeof(g_custom_label[1]));
	g_custom_label[1][sizeof(g_custom_label[1]) - 1] = '\0';
	char line[256];
	int pose_idx = 0;
	while (fgets(line, sizeof(line), f)) {
		coords_trim_line(line);
		if (!line[0] || line[0] == '#')
			continue;
		if (strncmp(line, "face_lim", 8) == 0 && (line[8] == ' ' || line[8] == '\t' || line[8] == '\0')) {
			int a, b;
			char *p = line + 8;
			while (*p == ' ' || *p == '\t') p++;
			if (sscanf(p, "%d %d", &a, &b) == 2 && a <= b) {
				g_face_min = a;
				g_face_max = b;
			}
			continue;
		}
		if (strncmp(line, "arm_lim", 7) == 0 && (line[7] == ' ' || line[7] == '\t' || line[7] == '\0')) {
			int a, b;
			char *p = line + 7;
			while (*p == ' ' || *p == '\t') p++;
			if (sscanf(p, "%d %d", &a, &b) == 2 && a <= b) {
				g_arm_min = a;
				g_arm_max = b;
			}
			continue;
		}
		if (strncmp(line, "spin", 4) == 0 && (line[4] == ' ' || line[4] == '\t' || line[4] == '\0')) {
			int a, b;
			char *p = line + 4;
			while (*p == ' ' || *p == '\t') p++;
			if (sscanf(p, "%d %d", &a, &b) == 2) {
				g_spin_face = a;
				g_spin_arm = b;
			}
			continue;
		}
		if (strncmp(line, "custom_labels", 13) == 0 &&
		    (line[13] == ' ' || line[13] == '\t' || line[13] == '\0')) {
			char a[40], b[40];
			char *p = line + 13;
			while (*p == ' ' || *p == '\t') p++;
			if (sscanf(p, "%39s %39s", a, b) == 2 && a[0] && b[0]) {
				strncpy(g_custom_label[0], a, sizeof(g_custom_label[0]));
				g_custom_label[0][sizeof(g_custom_label[0]) - 1] = '\0';
				strncpy(g_custom_label[1], b, sizeof(g_custom_label[1]));
				g_custom_label[1][sizeof(g_custom_label[1]) - 1] = '\0';
			}
			continue;
		}
		int f, a;
		if (sscanf(line, "%d %d", &f, &a) == 2) {
			if (pose_idx < POS_FIXED_COUNT) {
				face_positions[pose_idx] = f;
				arm_positions[pose_idx] = a;
				pose_idx++;
			}
		}
	}
	fclose(f);
	rebuild_fixed_positions_from_base();
	g_spin_face = clamp_face_hard(g_spin_face);
	g_spin_arm = clamp_arm_hard(g_spin_arm);
	last_pos1 = face_positions[0];
	last_pos2 = arm_positions[0];
}

static void save_coords_to_file(void)
{
	char path[600];
	FILE *f;
	coords_build_path(path, sizeof(path));
	f = fopen(path, "w");
	if (!f)
		return;
	fprintf(f, "# MrWatchmaker — exe와 같은 폴더에 두고 편집\n");
	fprintf(f, "# custom_labels: 고정 5·6번(윗면/아랫면 자리) 표시 이름\n");
	fprintf(f, "custom_labels %s %s\n", g_custom_label[0], g_custom_label[1]);
	fprintf(f, "\n# 1번(ID1 Face) 틱 제한 / 2번(ID2 Arm) 틱 제한\n");
	fprintf(f, "face_lim %d %d\n", g_face_min, g_face_max);
	fprintf(f, "arm_lim %d %d\n", g_arm_min, g_arm_max);
	fprintf(f, "\n# 스핀 기본값 참고 (Face, Arm) — 시작 위치는 실행 시 서보 Present로 잡힘\n");
	fprintf(f, "spin %d %d\n", g_spin_face, g_spin_arm);
	fprintf(f, "\n# 고정 6자세: 9시 → … → (cb). 각 줄 face arm\n");
	for (int i = 0; i < POS_FIXED_COUNT; i++)
		fprintf(f, "%d %d\n", face_positions[i], arm_positions[i]);
	fclose(f);
}

static void refresh_pos_coord_labels(struct output_panel *op)
{
	for (int i = 0; i < POS_FIXED_COUNT; i++) {
		if (!op->pos_coord_labels[i])
			continue;
		char buf[96];
		snprintf(buf, sizeof(buf), _("Face %d  │  Arm %d"), face_positions[i], arm_positions[i]);
		gtk_label_set_text(GTK_LABEL(op->pos_coord_labels[i]), buf);
	}
}

/* 미세조정 기준과 동일한 논리틱(Present − 시각보정 델타) */
static void update_micro_motor_readout_label_ui(struct output_panel *op)
{
	if (!op || !op->micro_motor_readout_label || !GTK_IS_WIDGET(op->micro_motor_readout_label))
		return;
	char buf[160];
	if (micro_adj_base_valid)
		snprintf(buf, sizeof(buf), _("Face %d  │  Arm %d"), micro_adj_base_face, micro_adj_base_arm);
	else
		snprintf(buf, sizeof(buf), _("Face %d  │  Arm %d  (읽기 재시도 중)"), last_pos1, last_pos2);
	gtk_label_set_text(GTK_LABEL(op->micro_motor_readout_label), buf);
}

static gboolean refresh_micro_motor_label_idle(gpointer data)
{
	struct output_panel *op = data;
	update_micro_motor_readout_label_ui(op);
	return G_SOURCE_REMOVE;
}

/* Present 읽기 → 기준점 미세조정 숫자·last_pos (포트 열린 상태에서 호출, motor_close는 호출자) */
static void micro_adj_refresh_from_present_reads(int r1, int r2, struct output_panel *op)
{
	int vdF, vdA;

	motor_get_visual_goal_deltas(&vdF, &vdA);
	if (r1 >= 0 && r2 >= 0) {
		micro_adj_base_face = logical_face_from_raw(r1, vdF);
		micro_adj_base_arm = logical_arm_from_raw(r2, vdA);
		micro_adj_base_valid = 1;
	} else {
		micro_adj_base_valid = 0;
	}
	if (r1 >= 0) {
		last_pos1 = clamp_face_hard(r1);
		g_spin_face = last_pos1;
	}
	if (r2 >= 0) {
		last_pos2 = clamp_arm_hard(r2);
		g_spin_arm = last_pos2;
	}
	if (op)
		g_idle_add(refresh_micro_motor_label_idle, op);
}

static void auto_measure_set_button_countdown_label(struct output_panel *op)
{
	char buf[192];
	char posebuf[72];
	int pose_idx;
	const char *pname;

	if (!op->auto_measure_button)
		return;
	pose_idx = op->auto_measure_state - 1;
	if (pose_idx >= 0 && pose_idx < POS_FIXED_COUNT) {
		if (pose_idx == 0)
			pname = _("🕘 9시 (기본)");
		else if (pose_idx == 1)
			pname = _("🕛 12시");
		else if (pose_idx == 2)
			pname = _("🕒 3시");
		else if (pose_idx == 3)
			pname = _("🕕 6시");
		else {
			snprintf(posebuf, sizeof(posebuf), _("★%s"), g_custom_label[pose_idx - 4]);
			pname = posebuf;
		}
		snprintf(buf, sizeof(buf), _("자세: %s — 측정 중... %d 초"), pname,
			op->auto_measure_countdown);
	} else {
		snprintf(buf, sizeof(buf), _("측정 중... %d 초"), op->auto_measure_countdown);
	}
	gtk_button_set_label(GTK_BUTTON(op->auto_measure_button), buf);
}

// 이동 거리에 따라 속도를 자동 계산 (최소 5초, 거리가 클수록 더 느리게)
static int calc_duration(int from, int to) {
	int dist = from - to;
	if (dist < 0) dist = -dist;
	int ms = 5000 + (dist * 3); // 거리 1당 3ms 추가 (최소 5초)
	if (ms > 12000) ms = 12000; // 최대 12초
	return ms;
}

/* 상시 토크 해제 기조를 유지하되, 실제 이동/미세조정 직전에는
 * 일시적으로 토크를 복구해야 암/페이스가 모두 반응한다. */
static void motor_ensure_torque_on(void) {
	motor_write_byte(1, 0x28, 1); /* Torque Enable = 1 */
	motor_write_byte(2, 0x28, 1); /* Torque Enable = 1 */
	motor_write_word(1, 48, 1000);
	motor_write_word(2, 48, 1000);
}

#define ARM_RELEASE_POLL_MS 250

typedef struct {
	struct output_panel *op;
	int r1;
	int r2;
} arm_release_poll_packet_t;

static gboolean arm_release_poll_apply_idle(gpointer data)
{
	arm_release_poll_packet_t *p = (arm_release_poll_packet_t *)data;
	if (p && p->op == s_arm_release_poll_op)
		micro_adj_refresh_from_present_reads(p->r1, p->r2, p->op);
	g_free(p);
	return G_SOURCE_REMOVE;
}

static gboolean arm_release_poll_init_fail_idle(gpointer data)
{
	struct output_panel *op = (struct output_panel *)data;
	if (op == s_arm_release_poll_op)
		micro_adj_refresh_from_present_reads(-1, -1, op);
	return G_SOURCE_REMOVE;
}

static gboolean arm_release_poll_join_thread_idle(gpointer data)
{
	GThread *t = (GThread *)data;
	if (t && t == s_arm_release_poll_thread) {
		s_arm_release_poll_thread = NULL;
		g_thread_join(t);
	}
	return G_SOURCE_REMOVE;
}

static gpointer arm_release_live_poll_thread_func(gpointer data)
{
	struct output_panel *op = (struct output_panel *)data;

	if (!motor_init(motor_get_port())) {
		g_idle_add(arm_release_poll_init_fail_idle, op);
		/* 스레드가 곧 종료되므로 메인에서 g_thread_join 으로 정리 */
		g_idle_add(arm_release_poll_join_thread_idle, g_thread_self());
		return NULL;
	}

	while (g_atomic_int_get(&s_arm_release_poll_run)) {
		if (op != s_arm_release_poll_op)
			break;
		int r1 = motor_read_present_position(1);
		int r2 = motor_read_present_position(2);
		if (g_atomic_int_get(&s_arm_release_poll_run) && op == s_arm_release_poll_op) {
			arm_release_poll_packet_t *p = g_new(arm_release_poll_packet_t, 1);
			p->op = op;
			p->r1 = r1;
			p->r2 = r2;
			g_idle_add(arm_release_poll_apply_idle, p);
		}
		g_usleep(ARM_RELEASE_POLL_MS * 1000U);
	}
	motor_close();
	return NULL;
}

static void arm_release_live_poll_stop(void)
{
	g_atomic_int_set(&s_arm_release_poll_run, 0);
	s_arm_release_poll_op = NULL;
	if (s_arm_release_poll_thread) {
		GThread *t = s_arm_release_poll_thread;
		s_arm_release_poll_thread = NULL;
		g_thread_join(t);
	}
}

static void arm_release_live_poll_start(struct output_panel *op)
{
	if (!op)
		return;
	arm_release_live_poll_stop();
	s_arm_release_poll_op = op;
	g_atomic_int_set(&s_arm_release_poll_run, 1);
	s_arm_release_poll_thread = g_thread_new("arm_rel_poll", arm_release_live_poll_thread_func, op);
}

/* 와인더 정지·자세차 종료/취소 시 목표 틱 기준 🕘9시로 복귀 → 토크 해제 → Present로 기준점 미세조정 반영
 * op_for_label_idle == NULL 이면 micro_adj 라벨용 g_idle_add 생략(프로그램 종료 직전 등) */
static void motor_return_to_9h_pose_inner(struct output_panel *op_for_label_idle)
{
	int dur1, dur2;
	int p1 = clamp_face(face_positions[0]);
	int p2 = clamp_arm(arm_positions[0]);

	arm_release_live_poll_stop();
	if (!motor_init(motor_get_port()))
		return;
	motor_ensure_torque_on();
	dur1 = calc_duration(last_pos1, p1);
	dur2 = calc_duration(last_pos2, p2);
	motor_move(1, p1, dur1, 0);
	g_usleep(100000);
	/* cb -> 9시 복귀에서 ID2(암) 명령이 간헐적으로 씹히는 경우를 줄이기 위해
	 * 암 토크를 한 번 더 확실히 올리고 이동 명령을 보강한다. */
	motor_write_byte(2, 0x28, 1);
	motor_write_word(2, 48, 1000);
	motor_move(2, p2, dur2, 0);
	g_usleep(120000);
	motor_move(2, p2, dur2, 0);
	g_usleep((dur2 > dur1 ? dur2 : dur1) * 1000);
	/* 최종 9시 복귀 도착 확인: 오차가 크면 한 번 더 보정 이동 */
	{
		int r1_chk = motor_read_present_position(1);
		int r2_chk = motor_read_present_position(2);
		int need_retry = 0;
		if (r1_chk >= 0 && abs(r1_chk - p1) > 80)
			need_retry = 1;
		if (r2_chk >= 0 && abs(r2_chk - p2) > 100)
			need_retry = 1;
		if (need_retry) {
			int retry1 = calc_duration(r1_chk >= 0 ? r1_chk : last_pos1, p1);
			int retry2 = calc_duration(r2_chk >= 0 ? r2_chk : last_pos2, p2);
			if (retry1 < 1200) retry1 = 1200;
			if (retry2 < 1200) retry2 = 1200;
			motor_ensure_torque_on();
			motor_move(1, p1, retry1, 0);
			g_usleep(100000);
			motor_write_byte(2, 0x28, 1);
			motor_write_word(2, 48, 1000);
			motor_move(2, p2, retry2, 0);
			g_usleep((retry2 > retry1 ? retry2 : retry1) * 1000);
		}
	}
	motor_disable_torque_all();
	g_usleep(80000);
	{
		int r1 = motor_read_present_position(1);
		int r2 = motor_read_present_position(2);

		micro_adj_refresh_from_present_reads(r1, r2, op_for_label_idle);
	}
	motor_close();
}

static void motor_return_to_9h_pose(struct output_panel *op)
{
	motor_return_to_9h_pose_inner(op);
}

void op_shutdown_motor_home_9h(struct output_panel *op)
{
	if (!op)
		return;
	if (op->winder_timeout_id != 0) {
		g_source_remove(op->winder_timeout_id);
		op->winder_timeout_id = 0;
	}
	op->winder_active = 0;
	motor_return_to_9h_pose_inner(NULL);
}

/* 시작 시 서보 Present → last_pos / 스핀 기본값 (이동 시간이 실제 위치에서 계산되도록) */
static gpointer sync_last_pos_from_servos_thread(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;

	/* idle 로 poll_stop 하면 메인이 바쁠 때 훨씬 늦게 실행되어 암 풀기 후 실시간 폴링만 끊는 경우가 있음 */
	if (!motor_init(motor_get_port())) {
		micro_adj_base_valid = 0;
		if (op) g_idle_add(refresh_micro_motor_label_idle, op);
		return NULL;
	}
	motor_ensure_torque_on();
	g_usleep(300000);
	{
		int r1 = -1, r2 = -1;
		for (int attempt = 0; attempt < 3 && (r1 < 0 || r2 < 0); attempt++) {
			if (r1 < 0) r1 = motor_read_present_position(1);
			if (r2 < 0) r2 = motor_read_present_position(2);
			if (r1 < 0 || r2 < 0)
				g_usleep(120000);
		}
		/* 한쪽만 읽혀도 숫자 표시는 유지되도록 마지막 위치를 보존한다. */
		if (r1 < 0) r1 = last_pos1;
		if (r2 < 0) r2 = last_pos2;

		micro_adj_refresh_from_present_reads(r1, r2, op);
	}
	motor_disable_torque_all();
	motor_close();
	return NULL;
}

/* ── 수동 자세별 측정(버튼): 해당 자세로 이동 후 카운트다운 뒤 값 저장 ───────── */
typedef struct { struct output_panel *op; int target; } manual_move_data_t;

static void set_manual_buttons_enabled(struct output_panel *op, int enabled) {
	for (int i = 0; i < POS_FIXED_COUNT; i++) {
		if (op->manual_measure_buttons[i])
			gtk_widget_set_sensitive(op->manual_measure_buttons[i], enabled);
	}
	if (op->auto_measure_button)
		gtk_widget_set_sensitive(op->auto_measure_button, enabled);
}

static void manual_reset_button_labels(struct output_panel *op) {
	static const char *fixed_names[4] = {
		"🕘 9시 측정", "🕛 12시 측정", "🕒 3시 측정", "🕕 6시 측정",
	};
	for (int i = 0; i < 4; i++)
		if (op->manual_measure_buttons[i])
			gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[i]), _(fixed_names[i]));
	for (int i = 4; i < POS_FIXED_COUNT; i++)
		if (op->manual_measure_buttons[i]) {
			char buf[64];
			snprintf(buf, sizeof(buf), _("★%s 측정"), g_custom_label[i - 4]);
			gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[i]), buf);
		}
}

static gboolean manual_measure_tick(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	if (op->manual_measure_target < 0 || op->manual_measure_target >= POS_FIXED_COUNT)
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
		/* 수동 자세별 측정 후에도 🕘9시(기본)로 복귀 (자동 측정과 동일) */
		if (op->manual_measure_buttons[idx])
			gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[idx]), _("⏎  베이스로 복귀 중..."));
		motor_return_to_9h_pose(op);
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
	if (t < 0 || t >= POS_FIXED_COUNT) return NULL;

	int p1 = clamp_face(face_positions[t]);
	int p2 = clamp_arm(arm_positions[t]);
	int dur1 = calc_duration(last_pos1, p1);
	int dur2 = calc_duration(last_pos2, p2);

	motor_init(motor_get_port());
	motor_ensure_torque_on();
	motor_move(1, p1, dur1, 0);
	g_usleep(100000);
	motor_move(2, p2, dur2, 0);
	g_usleep((dur2 > dur1 ? dur2 : dur1) * 1000);
	motor_disable_torque_all();
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

	arm_release_live_poll_stop();
	int target = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "manual_target"));
	if (target < 0 || target >= POS_FIXED_COUNT) return;

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

/* 슬롯 0..5 고정 6자세 */
static void get_positional_slot_face_arm(struct output_panel *op, int slot_idx, int *out_p1, int *out_p2) {
	(void)op;
	if (slot_idx < 0 || slot_idx >= POS_FIXED_COUNT)
		slot_idx = 0;
	*out_p1 = face_positions[slot_idx];
	*out_p2 = arm_positions[slot_idx];
}

static void show_servo_connect_error_dialog(struct output_panel *op) {
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
}

static GMutex micro_rep_mutex;
static int micro_rep_kind = MICRO_REP_NONE;
static struct output_panel *micro_rep_op;
static GThread *micro_rep_thread;
/* Face/Arm ± 홀드 중 라벨 실시간 갱신 (워커→메인, idle 합류) */
static volatile int s_micro_live_idle_scheduled;
static int s_micro_live_face;
static int s_micro_live_arm;
static struct output_panel *s_micro_live_op;

static gboolean micro_motor_live_label_idle(gpointer data)
{
	(void)data;
	s_micro_live_idle_scheduled = 0;
	if (s_micro_live_op && s_micro_live_op->micro_motor_readout_label &&
	    GTK_IS_WIDGET(s_micro_live_op->micro_motor_readout_label)) {
		char buf[160];
		snprintf(buf, sizeof(buf), _("Face %d  │  Arm %d"), s_micro_live_face, s_micro_live_arm);
		gtk_label_set_text(GTK_LABEL(s_micro_live_op->micro_motor_readout_label), buf);
	}
	return G_SOURCE_REMOVE;
}

static void micro_motor_schedule_live_readout(int face_log, int arm_log, struct output_panel *op)
{
	s_micro_live_face = face_log;
	s_micro_live_arm = arm_log;
	s_micro_live_op = op;
	/* 미세조정 홀드 중 화면에 보이는 기준점 숫자와
	 * 자동측정 시작 검증 기준을 동일하게 유지한다. */
	micro_adj_base_face = face_log;
	micro_adj_base_arm = arm_log;
	micro_adj_base_valid = 1;
	if (!s_micro_live_idle_scheduled) {
		s_micro_live_idle_scheduled = 1;
		g_idle_add(micro_motor_live_label_idle, NULL);
	}
}


static void micro_rep_stop(void)
{
	g_mutex_lock(&micro_rep_mutex);
	micro_rep_kind = MICRO_REP_NONE;
	micro_rep_op = NULL;
	g_mutex_unlock(&micro_rep_mutex);
}

/* 연속 미세조정: 재시도·대기 시간 단축 (세리얼 한 번에 한 워커만 사용) */
static int micro_read_both_present_fast(int *rF, int *rA)
{
	for (int attempt = 0; attempt < 3; attempt++) {
		*rF = motor_read_present_position(1);
		g_usleep(45000);
		*rA = motor_read_present_position(2);
		if (*rF >= 0 && *rA >= 0)
			return 1;
		*rA = motor_read_present_position(2);
		g_usleep(45000);
		*rF = motor_read_present_position(1);
		if (*rF >= 0 && *rA >= 0)
			return 1;
		g_usleep(70000);
	}
	return 0;
}

static gpointer micro_rep_worker(gpointer unused);

static void micro_rep_start(int kind, struct output_panel *op)
{
	g_mutex_lock(&micro_rep_mutex);
	micro_rep_kind = kind;
	micro_rep_op = op;
	if (!micro_rep_thread)
		micro_rep_thread = g_thread_new("micro_rep", micro_rep_worker, NULL);
	g_mutex_unlock(&micro_rep_mutex);
}

static gpointer micro_rep_worker(gpointer unused)
{
	(void)unused;
	int k;
	struct output_panel *op;
	int motor_opened = 0;

	g_mutex_lock(&micro_rep_mutex);
	k = micro_rep_kind;
	op = micro_rep_op;
	g_mutex_unlock(&micro_rep_mutex);
	if (k == MICRO_REP_NONE || !op)
		return NULL;

	struct main_window *mw = g_object_get_data(G_OBJECT(op->panel), "main-window");
	if (!mw) {
		micro_rep_stop();
		goto worker_cleanup;
	}

	if (!motor_init(motor_get_port())) {
		schedule_servo_connect_error_dialog_once(op);
		micro_rep_stop();
		goto worker_cleanup;
	}
	motor_opened = 1;
	motor_ensure_torque_on();
	g_usleep(25000);

	for (;;) {
		g_mutex_lock(&micro_rep_mutex);
		k = micro_rep_kind;
		op = micro_rep_op;
		g_mutex_unlock(&micro_rep_mutex);
		if (k == MICRO_REP_NONE || !op)
			break;

		mw = g_object_get_data(G_OBJECT(op->panel), "main-window");
		if (!mw) {
			micro_rep_stop();
			break;
		}

		int rF, rA;
		if (!micro_read_both_present_fast(&rF, &rA)) {
			/* 실측을 못 읽은 상태에서는 목표값 기반으로 점프시키지 않는다.
			 * 다음 루프에서 재시도하도록 하고 현재 값은 유지한다. */
			g_usleep(MICRO_HOLD_GAP_US);
			continue;
		}
		int vdF = mw->visual_delta_face;
		int vdA = mw->visual_delta_arm;
		int lf = logical_face_from_raw(rF, vdF);
		int la = logical_arm_from_raw(rA, vdA);

		if (k == MICRO_REP_FACE_M || k == MICRO_REP_FACE_P) {
			int step = (k == MICRO_REP_FACE_M) ? -MICRO_STEP_FACE : MICRO_STEP_FACE;
			int next_lf = clamp_face_hard(norm4096_pc(lf + step));
			motor_move(1, next_lf, MICRO_NUDGE_MOVE_MS, 0);
			g_usleep(35000);
			motor_move(2, la, 280, 0);
			{
				int rrF = motor_read_present_position(1);
				int rrA = motor_read_present_position(2);
				if (rrF >= 0 && rrA >= 0) {
					int lf2 = logical_face_from_raw(rrF, vdF);
					int la2 = logical_arm_from_raw(rrA, vdA);
					last_pos1 = clamp_face_hard(rrF);
					last_pos2 = clamp_arm_hard(rrA);
					micro_motor_schedule_live_readout(lf2, la2, op);
				} else {
					last_pos1 = next_lf;
					last_pos2 = la;
					micro_motor_schedule_live_readout(next_lf, la, op);
				}
			}
		} else if (k == MICRO_REP_ARM_M || k == MICRO_REP_ARM_P) {
			int step = (k == MICRO_REP_ARM_M) ? -MICRO_STEP_ARM : MICRO_STEP_ARM;
			int next_la = clamp_arm_hard(norm4096_pc(la + step));
			/* ARM 홀드 미세조정 중에는 토크가 간헐적으로 풀려 입력이 끊길 수 있어
			 * 매 반복마다 암 토크/리밋을 다시 복구한다. */
			motor_write_byte(2, 0x28, 1);
			motor_write_word(2, 48, 1000);
			motor_move(2, next_la, MICRO_NUDGE_MOVE_MS, 0);
			{
				int rrF = motor_read_present_position(1);
				int rrA = motor_read_present_position(2);
				if (rrF >= 0 && rrA >= 0) {
					int lf2 = logical_face_from_raw(rrF, vdF);
					int la2 = logical_arm_from_raw(rrA, vdA);
					last_pos1 = clamp_face_hard(rrF);
					last_pos2 = clamp_arm_hard(rrA);
					micro_motor_schedule_live_readout(lf2, la2, op);
				} else {
					last_pos1 = lf;
					last_pos2 = next_la;
					micro_motor_schedule_live_readout(lf, next_la, op);
				}
			}
		}

		g_usleep(MICRO_HOLD_GAP_US);
	}
worker_cleanup:
	if (motor_opened) {
		motor_disable_torque_all();
		motor_close();
	}
	g_mutex_lock(&micro_rep_mutex);
	micro_rep_thread = NULL;
	g_mutex_unlock(&micro_rep_mutex);
	return NULL;
}

typedef struct {
	struct output_panel *op;
	int dF;
	int dA;
} batch_apply_done_t;

static gboolean batch_apply_done_idle(gpointer data) {
	batch_apply_done_t *bd = (batch_apply_done_t *)data;
	refresh_pos_coord_labels(bd->op);
	update_micro_motor_readout_label_ui(bd->op);
	char buf[256];
	snprintf(buf, sizeof(buf), _("6자세 목표 좌표에 반영했습니다.\nFace Δ%+d, Arm Δ%+d"), bd->dF, bd->dA);
	GtkWidget *win = gtk_widget_get_toplevel(bd->op->panel);
	if (!GTK_IS_WINDOW(win)) win = NULL;
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", buf);
	gtk_window_set_title(GTK_WINDOW(dlg), _("기준점 일괄적용"));
	if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
	gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	g_free(bd);
	return G_SOURCE_REMOVE;
}

static gboolean batch_apply_err_baseline_idle(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	GtkWidget *win = gtk_widget_get_toplevel(op->panel);
	if (!GTK_IS_WINDOW(win)) win = NULL;
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
		"%s", _("미세조정 기준이 없습니다. 서보 연결 후 Face/Arm ±로 조정한 다음 다시 누르세요."));
	gtk_window_set_title(GTK_WINDOW(dlg), _("기준점 일괄적용"));
	if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
	gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	return G_SOURCE_REMOVE;
}

static gboolean batch_apply_noop_idle(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	GtkWidget *win = gtk_widget_get_toplevel(op->panel);
	if (!GTK_IS_WINDOW(win)) win = NULL;
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
		"%s", _("적용할 미세조정 변화가 없습니다. Face/Arm ±로 먼저 조정하세요."));
	gtk_window_set_title(GTK_WINDOW(dlg), _("기준점 일괄적용"));
	if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
	gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	return G_SOURCE_REMOVE;
}

typedef struct { struct output_panel *op; } batch_apply_param_t;

static gpointer batch_apply_thread(gpointer data) {
	batch_apply_param_t *p = (batch_apply_param_t *)data;
	struct output_panel *op = p->op;
	g_free(p);
	if (!micro_adj_base_valid) {
		g_idle_add(batch_apply_err_baseline_idle, op);
		return NULL;
	}
	/* 일괄적용 기준은 항상 UI에 보이는 미세조정 좌표를 사용한다.
	 * (last_pos는 raw/논리틱이 섞일 수 있어 9시 저장값이 틀어질 수 있음) */
	int curF = micro_adj_base_face;
	int curA = micro_adj_base_arm;
	int dF = tick_delta_signed(face_positions[0], curF);
	int dA = tick_delta_signed(arm_positions[0], curA);
	if (dF == 0 && dA == 0) {
		g_idle_add(batch_apply_noop_idle, op);
		return NULL;
	}
	/* 기준점 적용은 누적 보정이 아니라 "현재 미세조정 값 = 9시 기본값"으로 고정 */
	face_positions[0] = clamp_face_hard(norm4096_pc(curF));
	arm_positions[0] = clamp_arm_hard(norm4096_pc(curA));
	rebuild_fixed_positions_from_base();
	save_coords_to_file();
	last_pos1 = face_positions[0];
	last_pos2 = arm_positions[0];
	micro_adj_base_face = curF;
	micro_adj_base_arm = curA;
	batch_apply_done_t *bd = g_new(batch_apply_done_t, 1);
	bd->op = op;
	bd->dF = dF;
	bd->dA = dA;
	g_idle_add(batch_apply_done_idle, bd);
	return NULL;
}

void on_batch_apply_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;

	arm_release_live_poll_stop();
	batch_apply_param_t *p = g_new(batch_apply_param_t, 1);
	p->op = op;
	g_thread_new("batch_apply", batch_apply_thread, p);
}

static gchar *reference_9oclock_image_path(void)
{
	gchar *p;
#ifdef G_OS_WIN32
	gchar *dir = g_win32_get_package_installation_directory_of_module(NULL);
	if (dir) {
		p = g_build_filename(dir, REF_9OCLOCK_IMAGE_FILE, NULL);
		g_free(dir);
		if (g_file_test(p, G_FILE_TEST_IS_REGULAR))
			return p;
		g_free(p);
	}
#endif
	p = g_build_filename(".", REF_9OCLOCK_IMAGE_FILE, NULL);
	if (g_file_test(p, G_FILE_TEST_IS_REGULAR))
		return p;
	g_free(p);
	return NULL;
}

static void on_ref9_dialog_response(GtkDialog *dlg, gint response_id, gpointer user_data)
{
	(void)response_id;
	(void)user_data;
	gtk_widget_destroy(GTK_WIDGET(dlg));
}

static void on_ref9_parent_destroy(GtkWidget *parent, gpointer user_data)
{
	(void)parent;
	GtkWidget *dlg = (GtkWidget *)user_data;
	if (dlg && GTK_IS_WIDGET(dlg))
		gtk_widget_destroy(dlg);
}

static void on_ref9_dialog_destroy(GtkWidget *w, gpointer user_data)
{
	(void)w;
	(void)user_data;
	s_ref9_window = NULL;
}

/* 참고 사진: 책상·벽 등 밝고 채도 낮은 부분을 흰색에 가깝게(누런 기 완화) */
#define REF9_WHITEN_AVG_MIN 130
#define REF9_WHITEN_SAT_MAX 55

static GdkPixbuf *ref9_pixbuf_whiten_light_background(GdkPixbuf *src)
{
	GdkPixbuf *dst;
	guchar *pixels;
	int w, h, rs, n, x, y;
	guchar *row;
	guchar *p;
	int r, g, b, mx, mn, avg, sat;
	int nr, ng, nb;
	float t;

	if (!src)
		return NULL;
	dst = gdk_pixbuf_copy(src);
	if (!dst)
		return NULL;
	w = gdk_pixbuf_get_width(dst);
	h = gdk_pixbuf_get_height(dst);
	rs = gdk_pixbuf_get_rowstride(dst);
	n = gdk_pixbuf_get_n_channels(dst);
	pixels = gdk_pixbuf_get_pixels(dst);
	for (y = 0; y < h; y++) {
		row = pixels + y * rs;
		for (x = 0; x < w; x++) {
			p = row + x * n;
			r = p[0];
			g = p[1];
			b = p[2];
			mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
			mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
			avg = (r + g + b) / 3;
			sat = mx - mn;
			if (avg > REF9_WHITEN_AVG_MIN && sat < REF9_WHITEN_SAT_MAX) {
				t = (float)(avg - REF9_WHITEN_AVG_MIN) / 125.0f;
				if (t > 1.0f)
					t = 1.0f;
				nr = (int)(r + (255 - r) * t * 0.45f);
				ng = (int)(g + (255 - g) * t * 0.48f);
				nb = (int)(b + (255 - b) * t * 0.58f);
				if (nr > 255)
					nr = 255;
				if (ng > 255)
					ng = 255;
				if (nb > 255)
					nb = 255;
				p[0] = (guchar)nr;
				p[1] = (guchar)ng;
				p[2] = (guchar)nb;
			}
		}
	}
	return dst;
}

static gboolean idle_ref9_position_beside_parent(gpointer data)
{
	GtkWindow *dlg = GTK_WINDOW(data);
	GtkWindow *parent = g_object_get_data(G_OBJECT(dlg), REF9_PARENT_KEY);
	int dw, dh;
	int px, py, pw_w, pw_h;
	int x, y, margin;
	GdkWindow *gdkw;

	if (!parent)
		return FALSE;
	gdkw = gtk_widget_get_window(GTK_WIDGET(parent));
	if (!gdkw)
		return FALSE;
	gtk_window_get_size(dlg, &dw, &dh);
	gdk_window_get_origin(gdkw, &px, &py);
	pw_w = gdk_window_get_width(gdkw);
	pw_h = gdk_window_get_height(gdkw);
	margin = 8;
	/* 메인 창 오른쪽; 화면 밖이면 왼쪽 */
	x = px + pw_w + margin;
	y = py + (pw_h - dh) / 2;
#if GTK_CHECK_VERSION(3, 22, 0)
	{
		GdkDisplay *dpy = gtk_widget_get_display(GTK_WIDGET(dlg));
		GdkMonitor *mon = gdk_display_get_monitor_at_window(dpy, gdkw);
		GdkRectangle work;

		gdk_monitor_get_workarea(mon, &work);
		if (x + dw > work.x + work.width - margin)
			x = px - dw - margin;
		if (x < work.x + margin)
			x = work.x + margin;
		if (y + dh > work.y + work.height - margin)
			y = work.y + work.height - dh - margin;
		if (y < work.y + margin)
			y = work.y + margin;
	}
#endif
	gtk_window_move(dlg, x, y);
	return FALSE;
}

static void show_reference_9oclock_window(struct output_panel *op)
{
	GtkWindow *parent = NULL;
	if (op && op->panel && gtk_widget_get_toplevel(op->panel))
		parent = GTK_WINDOW(gtk_widget_get_toplevel(op->panel));

	if (s_ref9_window && GTK_IS_WIDGET(s_ref9_window)) {
		g_object_set_data(G_OBJECT(s_ref9_window), REF9_PARENT_KEY, parent);
		gtk_window_set_focus_on_map(GTK_WINDOW(s_ref9_window), FALSE);
		gtk_window_present(GTK_WINDOW(s_ref9_window));
		g_idle_add(idle_ref9_position_beside_parent, s_ref9_window);
		return;
	}

	/* transient_for 금지: Win32에서 부모 창이 항상 아래에 깔려 ± 클릭이 막힘 */
	GtkWidget *dlg = gtk_dialog_new_with_buttons(
		_("9시 자세 참고"),
		NULL,
		(GtkDialogFlags)0,
		_("닫기"), GTK_RESPONSE_CLOSE,
		NULL);
	gtk_window_set_modal(GTK_WINDOW(dlg), FALSE);
	gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_NONE);
	gtk_window_set_type_hint(GTK_WINDOW(dlg), GDK_WINDOW_TYPE_HINT_NORMAL);
	gtk_window_set_focus_on_map(GTK_WINDOW(dlg), FALSE);
	gtk_window_set_keep_above(GTK_WINDOW(dlg), FALSE);
	g_object_set_data(G_OBJECT(dlg), REF9_PARENT_KEY, parent);
	s_ref9_window = dlg;
	g_signal_connect(dlg, "response", G_CALLBACK(on_ref9_dialog_response), NULL);
	g_signal_connect(dlg, "destroy", G_CALLBACK(on_ref9_dialog_destroy), NULL);
	if (parent && GTK_IS_WIDGET(parent))
		g_signal_connect(parent, "destroy", G_CALLBACK(on_ref9_parent_destroy), dlg);

	{
		GtkCssProvider *css = gtk_css_provider_new();
		gtk_css_provider_load_from_data(css,
			"#ref9-dialog { background-color: #ffffff; }\n"
			"#ref9-photo-wrap { background-color: #ffffff; padding: 10px; }\n",
			-1, NULL);
		gtk_style_context_add_provider(gtk_widget_get_style_context(dlg),
			GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(css);
		gtk_widget_set_name(dlg, "ref9-dialog");
	}

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
	gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);

	const char *missing_txt =
		_("참고 이미지를 찾을 수 없습니다.\n(reference_9oclock.png을 mrwatchmaker.exe와 같은 폴더에 두세요.)");
	{
		GError *err = NULL;
		gchar *imgpath = reference_9oclock_image_path();
		int shown = 0;

		if (imgpath) {
			/* 작게 표시해 ± 영역을 덜 가림 */
			GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(imgpath, 380, 300, TRUE, &err);
			g_free(imgpath);
			if (pb) {
				GdkPixbuf *adj = ref9_pixbuf_whiten_light_background(pb);
				GdkPixbuf *use = adj ? adj : pb;

				if (adj)
					g_object_unref(pb);
				{
					GtkWidget *wrap = gtk_event_box_new();
					gtk_widget_set_name(wrap, "ref9-photo-wrap");
					GtkWidget *img = gtk_image_new_from_pixbuf(use);
					g_object_unref(use);
					gtk_container_add(GTK_CONTAINER(wrap), img);
					gtk_box_pack_start(GTK_BOX(vbox), wrap, FALSE, FALSE, 0);
					shown = 1;
				}
			}
			if (err)
				g_clear_error(&err);
		}
		if (!shown) {
			GtkWidget *lblmiss = gtk_label_new(missing_txt);
			gtk_label_set_line_wrap(GTK_LABEL(lblmiss), TRUE);
			gtk_widget_set_halign(lblmiss, GTK_ALIGN_CENTER);
			gtk_box_pack_start(GTK_BOX(vbox), lblmiss, FALSE, FALSE, 0);
		}
	}

	GtkWidget *hint = gtk_label_new(_("기준점 미세조정은 사진처럼 맞추시면 됩니다."));
	gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
	gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(vbox), hint, FALSE, FALSE, 0);

	gtk_widget_show_all(dlg);
	g_idle_add(idle_ref9_position_beside_parent, dlg);
}

static gboolean idle_show_ref9_on_micro_press(gpointer data)
{
	struct output_panel *op = data;
	show_reference_9oclock_window(op);
	return FALSE;
}

static gboolean on_micro_button_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	(void)w;
	if (!ev || ev->type != GDK_BUTTON_PRESS || ev->button != 1)
		return FALSE;
	micro_btn_ud_t *ud = data;
	if (!ud || !ud->op)
		return FALSE;
	arm_release_live_poll_stop();
	g_idle_add(idle_show_ref9_on_micro_press, ud->op);
	micro_rep_start(ud->kind, ud->op);
	return TRUE;
}

static gboolean on_micro_button_release(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	(void)w;
	(void)data;
	if (!ev || ev->type != GDK_BUTTON_RELEASE || ev->button != 1)
		return FALSE;
	micro_rep_stop();
	return TRUE;
}

static gboolean vis_cal_err_idle(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	servo_err_mutex_ensure();
	g_mutex_lock(&g_servo_err_sched_mutex);
	g_idle_servo_conn_err = 0;
	g_mutex_unlock(&g_servo_err_sched_mutex);
	show_servo_connect_error_dialog(op);
	return G_SOURCE_REMOVE;
}

static gboolean vis_cal_err_read_idle(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	servo_err_mutex_ensure();
	g_mutex_lock(&g_servo_err_sched_mutex);
	g_idle_servo_read_err = 0;
	g_mutex_unlock(&g_servo_err_sched_mutex);
	GtkWidget *win = gtk_widget_get_toplevel(op->panel);
	if (!GTK_IS_WINDOW(win)) win = NULL;
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
		GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
		"%s", _("서보 위치를 읽지 못했습니다."));
	gtk_window_set_title(GTK_WINDOW(dlg), _("기준점 저장"));
	if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
	gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	return G_SOURCE_REMOVE;
}

static gboolean arm_release_done_idle(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	GtkWidget *win = gtk_widget_get_toplevel(op->panel);
	if (!GTK_IS_WINDOW(win)) win = NULL;
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
		GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
		"%s", _("모터 토크를 해제했습니다. Face/Arm ±로 9시(기본)에 맞춘 뒤 자세차를 다시 시도하세요."));
	gtk_window_set_title(GTK_WINDOW(dlg), _("암풀기"));
	if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
	gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	arm_release_live_poll_start(op);
	return G_SOURCE_REMOVE;
}

static gpointer arm_release_thread_func(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;

	/* poll_stop 은 on_arm_release_jam_clicked 에서 메인 스레드에서 이미 호출함.
	 * 여기서 g_idle_add(stop) 하면 큐가 밀릴 때 폴링 시작 *이후*에 실행되어 실시간 표시가 멈춤 */
	g_usleep(150000);
	if (!motor_init(motor_get_port())) {
		schedule_servo_connect_error_dialog_once(op);
		return NULL;
	}
	motor_ensure_torque_on();
	motor_release_motors_for_jam();
	g_usleep(200000);
	{
		int r1 = motor_read_present_position(1);
		int r2 = motor_read_present_position(2);

		micro_adj_refresh_from_present_reads(r1, r2, op);
	}
	motor_close();
	g_idle_add(arm_release_done_idle, op);
	return NULL;
}

void on_arm_release_jam_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;
	arm_release_live_poll_stop();
	g_thread_new("arm_rel", arm_release_thread_func, op);
}

static gboolean auto_measure_tick(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	const int total = POS_FIXED_COUNT;

	if (op->auto_measure_state == 0) {
		return G_SOURCE_REMOVE;
	}

	op->auto_measure_countdown--;

	if (op->auto_measure_countdown > 0) {
		auto_measure_set_button_countdown_label(op);
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
		motor_return_to_9h_pose(op);
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
		int p1 = clamp_face(face_positions[next]);
		int p2 = clamp_arm(arm_positions[next]);
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
	auto_measure_set_button_countdown_label(op);
	return G_SOURCE_CONTINUE;
}

void on_auto_measure_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct output_panel *op = (struct output_panel *)data;

	if (op->auto_measure_state != 0) {
		// 취소 → 🕘9시(기본)로 복귀
		op->auto_measure_state = 0;
		gtk_button_set_label(GTK_BUTTON(op->auto_measure_button), _("⏎  베이스로 복귀 중..."));
		motor_return_to_9h_pose(op);
		gtk_button_set_label(GTK_BUTTON(op->auto_measure_button), _("▶  자세차 자동 측정"));
		return;
	}

	// 시작
	arm_release_live_poll_stop();
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
	g_usleep(300000);
	{
		int r1 = motor_read_present_position(1);
		int r2 = motor_read_present_position(2);
		if (r1 < 0 || r2 < 0) {
			motor_disable_torque_all();
			motor_close();
			GtkWidget *win = gtk_widget_get_toplevel(op->panel);
			if (!GTK_IS_WINDOW(win)) win = NULL;
			GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
				GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"%s", _("서보 위치를 읽지 못했습니다. USB·서보를 확인한 뒤 다시 시도하세요."));
			gtk_window_set_title(GTK_WINDOW(dlg), _("자세차 자동 측정"));
			if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
			gtk_dialog_run(GTK_DIALOG(dlg));
			gtk_widget_destroy(dlg);
			op->auto_measure_state = 0;
			return;
		}
		int vdF, vdA;
		motor_get_visual_goal_deltas(&vdF, &vdA);
		int lf = logical_face_from_raw(r1, vdF);
		int la = logical_arm_from_raw(r2, vdA);
		int dF = tick_delta_signed(face_positions[0], lf);
		int dA = tick_delta_signed(arm_positions[0], la);
		if (abs(dF) >= POS_AUTO_MEASURE_BASELINE_MAX_DELTA_TICKS
		    || abs(dA) >= POS_AUTO_MEASURE_BASELINE_MAX_DELTA_TICKS) {
			motor_disable_torque_all();
			motor_close();
			GtkWidget *win = gtk_widget_get_toplevel(op->panel);
			if (!GTK_IS_WINDOW(win)) win = NULL;
			GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win),
				GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
				"%s", _("기준점이 맞지 않습니다. 기준점 미세조정을 +-을 눌러 기본 자세가 되게 한 후 자세차 자동측정을 실행해 주세요. (이동이나 충격을 받지 않았다면 9시 측정을 누른 후 측정을 시작하면 됩니다.)"));
			gtk_window_set_title(GTK_WINDOW(dlg), _("자세차 자동 측정"));
			if (win) gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
			gtk_dialog_run(GTK_DIALOG(dlg));
			gtk_widget_destroy(dlg);
			op->auto_measure_state = 0;
			return;
		}
		last_pos1 = clamp_face_hard(r1);
		last_pos2 = clamp_arm_hard(r2);
		g_spin_face = last_pos1;
		g_spin_arm = last_pos2;
	}

	// 레이블 초기화 (고정 6자세)
	for (int i = 0; i < POS_FIXED_COUNT; i++) {
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
	auto_measure_set_button_countdown_label(op);
}

// ── Watch Winder ─────────────────────────────────────────────────────
// face/arm 인덱스: 9시=0 … 6시=3, (표시 ch)=4, (표시 cb)=5
/* 와인더는 밥 주기 용이므로 한 자세에 오래 멈추지 않게,
 * 정지 대기 시간은 0초(또는 최소)로 운용한다. */
#define WINDER_DWELL_SEC 0

typedef struct {
	int p1;
	int p2;
	int dur1;
	int dur2;
} winder_move_args_t;

static volatile gint s_winder_move_inflight = 0;

static gpointer winder_move_thread_func(gpointer data)
{
	winder_move_args_t *a = (winder_move_args_t *)data;
	if (!a) {
		g_atomic_int_set(&s_winder_move_inflight, 0);
		return NULL;
	}

	if (motor_init(motor_get_port())) {
		motor_ensure_torque_on();
		motor_move(1, a->p1, a->dur1, 0);
		g_usleep(100000);
		motor_move(2, a->p2, a->dur2, 0);
		last_pos1 = a->p1;
		last_pos2 = a->p2;
		g_usleep((a->dur2 > a->dur1 ? a->dur2 : a->dur1) * 1000);
		motor_disable_torque_all();
		motor_close();
	}

	g_free(a);
	g_atomic_int_set(&s_winder_move_inflight, 0);
	return NULL;
}

static void winder_move_to_step(struct output_panel *op) {
	const WinderPreset *p = &winder_presets[op->winder_preset];
	int total, pos_idx, p1, p2;

	if (op->winder_preset == WINDER_PRESET_POSITIONAL) {
		total = POS_FIXED_COUNT;
		if (total <= 0) total = 1;
		pos_idx = op->winder_state % total;
		get_positional_slot_face_arm(op, pos_idx, &p1, &p2);
		p1 = clamp_face_hard(p1);
		p2 = clamp_arm_hard(p2);
	} else {
		total = p->len;
		pos_idx = p->seq[op->winder_state % p->len];
		p1 = clamp_face_hard(face_positions[pos_idx]);
		p2 = clamp_arm_hard(arm_positions[pos_idx]);
	}

	int dur1 = winder_calc_duration(last_pos1, p1);
	int dur2 = winder_calc_duration(last_pos2, p2);

	if (g_atomic_int_get(&s_winder_move_inflight))
		return;
	{
		winder_move_args_t *a = g_new(winder_move_args_t, 1);
		a->p1 = p1;
		a->p2 = p2;
		a->dur1 = dur1;
		a->dur2 = dur2;
		g_atomic_int_set(&s_winder_move_inflight, 1);
		g_thread_new("winder_move", winder_move_thread_func, a);
	}

	/* 이 모드에서는 '도착 대기' 자체가 없음 */
	op->winder_countdown = 0;
}

static gboolean winder_tick(gpointer data) {
	struct output_panel *op = (struct output_panel *)data;
	if (!op->winder_active) return G_SOURCE_REMOVE;
	if (g_atomic_int_get(&s_winder_move_inflight))
		return G_SOURCE_CONTINUE;

	const WinderPreset *p = &winder_presets[op->winder_preset];
	int total;
	if (op->winder_preset == WINDER_PRESET_POSITIONAL)
		total = POS_FIXED_COUNT;
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
		if (op->winder_timeout_id != 0) {
			g_source_remove(op->winder_timeout_id);
			op->winder_timeout_id = 0;
		}
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
		g_atomic_int_set(&s_winder_move_inflight, 0);
		/* 정지 후 🕘9시(기본)로 복귀 (토크 해제·기준점 표시 동기화는 motor_return_to_9h_pose 내부) */
		motor_return_to_9h_pose(op);
		return;
	}

	// 프리셋 읽기
	op->winder_preset = gtk_combo_box_get_active(GTK_COMBO_BOX(op->winder_preset_combo));
	if (op->winder_preset < 0 || op->winder_preset >= WINDER_PRESET_COUNT)
		op->winder_preset = 0;

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
	arm_release_live_poll_stop();
	motor_ensure_torque_on();
	g_usleep(300000);
	{
		int r1 = motor_read_present_position(1);
		int r2 = motor_read_present_position(2);
		if (r1 >= 0) {
			last_pos1 = clamp_face_hard(r1);
			g_spin_face = last_pos1;
		}
		if (r2 >= 0) {
			last_pos2 = clamp_arm_hard(r2);
			g_spin_arm = last_pos2;
		}
	}
	motor_disable_torque_all();
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
	op->winder_timeout_id = g_timeout_add_seconds(WINDER_TICK_SEC, winder_tick, op);
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
	const int total = POS_FIXED_COUNT;
	int count = 0;

	double min_rate = 9999, max_rate = -9999;
	double sum_rate = 0, sum_amp = 0, sum_be = 0;
	int    min_idx = 0, max_idx = 0;

	for (int i = 0; i < total; i++) {
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
	const char *pos_names[] = {
		_("9시 (기본)"), _("12시"), _("3시"), _("6시"),
		g_custom_label[0], g_custom_label[1],
	};

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
	for (int i = 0; i < total; i++) {
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
