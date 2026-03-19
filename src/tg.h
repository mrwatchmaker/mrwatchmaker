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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <complex.h>
#include <fftw3.h>
#include <stdarg.h>
#include <gtk/gtk.h>
#include <pthread.h>

#ifdef __CYGWIN__
#define _WIN32
#endif

#define CONFIG_FILE_NAME "tg-timer.ini"

#define FILTER_CUTOFF 3000

#define CAL_DATA_SIZE 900

#define FIRST_STEP 1
#define FIRST_STEP_LIGHT 0

#define NSTEPS 4
#define PA_SAMPLE_RATE 44100
#define PA_BUFF_SIZE (PA_SAMPLE_RATE << (NSTEPS + FIRST_STEP))

#define OUTPUT_FONT 70
#define OUTPUT_WINDOW_HEIGHT 70

#define POSITIVE_SPAN 10
#define NEGATIVE_SPAN 25

#define EVENTS_COUNT 10000
#define EVENTS_MAX 100
#define PAPERSTRIP_ZOOM 10
#define PAPERSTRIP_ZOOM_CAL 100
#define PAPERSTRIP_MARGIN .2

#define MIN_BPH 12000
#define MAX_BPH 72000
#define DEFAULT_BPH 21600
#define MIN_LA 10 // deg
#define MAX_LA 90 // deg
#define DEFAULT_LA 52 // deg
#define MIN_CAL -1000 // 0.1 s/d
#define MAX_CAL 1000 // 0.1 s/d

#define PRESET_BPH { 12000, 14400, 18000, 19800, 21600, 25200, 28800, 36000, 43200, 72000, 0 };

#ifdef DEBUG
#define debug(...) print_debug(__VA_ARGS__)
#else
#define debug(...) {}
#endif

#define UNUSED(X) (void)(X)

/* algo.c */
struct processing_buffers {
	int sample_rate;
	int sample_count;
	float *samples, *samples_sc, *waveform, *waveform_sc, *tic_wf, *slice_wf, *tic_c;
	fftwf_complex *fft, *sc_fft, *tic_fft, *slice_fft;
	fftwf_plan plan_a, plan_b, plan_c, plan_d, plan_e, plan_f, plan_g;
	struct filter *hpf, *lpf;
	double period,sigma,be,waveform_max,phase,tic_pulse,toc_pulse,amp;
	double cal_phase;
	int waveform_max_i;
	int tic,toc;
	int ready;
	uint64_t timestamp, last_tic, last_toc, events_from;
	uint64_t *events;
#ifdef DEBUG
	int debug_size;
	float *debug;
#endif
};

struct calibration_data {
	int wp;
	int size;
	int state;
	double calibration;
	uint64_t start_time;
	double *times;
	double *phases;
	uint64_t *events;
};

void setup_buffers(struct processing_buffers *b);
void pb_destroy(struct processing_buffers *b);
struct processing_buffers *pb_clone(struct processing_buffers *p);
void pb_destroy_clone(struct processing_buffers *p);
void process(struct processing_buffers *p, int bph, double la, int light);
void setup_cal_data(struct calibration_data *cd);
void cal_data_destroy(struct calibration_data *cd);
int test_cal(struct processing_buffers *p);
int process_cal(struct processing_buffers *p, struct calibration_data *cd);

/* audio.c */
struct processing_data {
	struct processing_buffers *buffers;
	uint64_t last_tic;
	int is_light;
};

int start_portaudio(int *nominal_sample_rate, double *real_sample_rate);
int terminate_portaudio();
uint64_t get_timestamp(int light);
int analyze_pa_data(struct processing_data *pd, int bph, double la, uint64_t events_from);
int analyze_pa_data_cal(struct processing_data *pd, struct calibration_data *cd);

/* computer.c */
struct snapshot {
	struct processing_buffers *pb;
	int is_old;
	uint64_t timestamp;
	int is_light;

	int nominal_sr;
	int calibrate;
	int bph;
	double la; // deg
	int cal; // 0.1 s/d

	int events_count;
	uint64_t *events; // used in cal+timegrapher mode
	int events_wp; // used in cal+timegrapher mode
	uint64_t events_from; // used only in timegrapher mode

	int signal;

	int cal_state;
	int cal_percent;
	int cal_result; // 0.1 s/d

	// data dependent on bph, la, cal
	double sample_rate;
	int guessed_bph;
	double rate;
	double be;
	double amp;

	double trace_centering;
};

struct computer {
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

// controlled by interface
	int recompute;
	int calibrate;
	int bph;
	double la; // deg
	int clear_trace;
	void (*callback)(void *);
	void *callback_data;

	struct processing_data *pdata;
	struct calibration_data *cdata;

	struct snapshot *actv;
	struct snapshot *curr;
};

struct snapshot *snapshot_clone(struct snapshot *s);
void snapshot_destroy(struct snapshot *s);
void computer_destroy(struct computer *c);
struct computer *start_computer(int nominal_sr, int bph, double la, int cal, int light);
void lock_computer(struct computer *c);
void unlock_computer(struct computer *c);
void compute_results(struct snapshot *s);

/* output_panel.c */
struct output_panel {
	GtkWidget *panel;

	GtkWidget *output_drawing_area;
	GtkWidget *tic_drawing_area;
	GtkWidget *toc_drawing_area;
	GtkWidget *period_drawing_area;
	GtkWidget *paperstrip_drawing_area;
	GtkWidget *clear_button;

	// Positional error labels (0-3: fixed, 4-7: custom)
	GtkWidget *pos_labels[8];
	GtkWidget *custom_name_labels[4]; // 커스텀 행 이름 레이블
	GtkWidget *custom_rows[4];        // 커스텀 행 전체 (show/hide)
	int num_custom;                   // 추가된 커스텀 자세 수 (0~4)
	GtkWidget *auto_measure_button;
	int auto_measure_state;
	guint auto_measure_timer;
	int auto_measure_countdown;
	// 수동 자세별 측정(버튼)
	GtkWidget *manual_measure_buttons[8]; // 0..3=고정(9/12/3/6), 4..7=커스텀 1..4
	int manual_measure_target;            // -1=비활성, 0..7=타겟
	int manual_measure_countdown;         // 초 카운트다운
	guint manual_measure_timer;

	// Watch Winder
	GtkWidget *winder_button;
	GtkWidget *winder_status_label;
	GtkWidget *winder_preset_combo;
	int winder_active;    // 0=정지, 1=동작중
	int winder_state;     // 현재 시퀀스 인덱스
	int winder_countdown; // (legacy) ms 카운트다운
	int winder_cycles;    // 완료된 순환 횟수
	int winder_preset;    // 선택된 프리셋 인덱스
	int winder_tick_ms;   // winder tick 간격(ms)

	/* 와인더 연속 궤적(trajectory) */
	int winder_serial_open;     // 시리얼 열림 여부
	int winder_seg_total_ms;    // 현재 구간 총 시간
	int winder_seg_elapsed_ms;  // 현재 구간 경과 시간
	int winder_from1, winder_from2;
	int winder_to1,   winder_to2;

	// 자세차 측정 진단 리포트
	GtkWidget *analysis_scroll;    // 스크롤 컨테이너
	GtkWidget *analysis_textview;  // 분석 텍스트뷰
	double  pos_rate[8];           // 자세별 레이트 (s/d)
	double  pos_amp[8];            // 자세별 진폭 (°)
	double  pos_be[8];             // 자세별 비트에러 (ms)
	int     pos_measured[8];       // 측정 완료 여부

#ifdef DEBUG
	GtkWidget *debug_drawing_area;
#endif
	struct computer *computer;
	struct snapshot *snst;
};

void initialize_palette();
struct output_panel *init_output_panel(struct computer *comp, struct snapshot *snst, int border);
void redraw_op(struct output_panel *op);
void op_set_snapshot(struct output_panel *op, struct snapshot *snst);
void op_set_border(struct output_panel *op, int i);
void op_destroy(struct output_panel *op);

/* interface.c */
struct main_window {
	GtkApplication *app;

	GtkWidget *window;
	GtkWidget *bph_combo_box;
	GtkWidget *la_spin_button;
	GtkWidget *cal_spin_button;
	GtkWidget *snapshot_button;
	GtkWidget *snapshot_name;
	GtkWidget *snapshot_name_entry;
	GtkWidget *cal_button;
	GtkWidget *notebook;
	GtkWidget *save_item;
	GtkWidget *save_all_item;
	GtkWidget *close_all_item;
	struct output_panel *active_panel;

	struct computer *computer;
	struct snapshot *active_snapshot;
	int computer_timeout;

	int is_light;
	int zombie;
	int controls_active;
	int calibrate;
	int bph;
	double la; // deg
	int cal; // 0.1 s/d
	int nominal_sr;
	int lang;

	GKeyFile *config_file;
	gchar *config_file_name;
	struct conf_data *conf_data;

	guint kick_timeout;
	guint save_timeout;
};

extern int preset_bph[];

#ifdef DEBUG
extern int testing;
#endif

void print_debug(char *format,...);
void error(char *format,...);

/* config.c */
#define CONFIG_FIELDS(OP) \
	OP(bph, bph, int) \
	OP(lift_angle, la, double) \
	OP(calibration, cal, int) \
	OP(light_algorithm, is_light, int) \
	OP(language, lang, int)

struct conf_data {
#define DEF(NAME,PLACE,TYPE) TYPE PLACE;
	CONFIG_FIELDS(DEF)
};

void load_config(struct main_window *w);
void save_config(struct main_window *w);
void save_on_change(struct main_window *w);
void close_config(struct main_window *w);

/* serializer.c */
int write_file(FILE *f, struct snapshot **s, char **names, uint64_t cnt);
int read_file(FILE *f, struct snapshot ***s, char ***names, uint64_t *cnt);
