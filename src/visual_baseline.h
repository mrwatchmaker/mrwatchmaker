/*
 * CH(다이얼 업) 카메라 기준점 — UVC 캡처·참조 이미지 대조
 */
#ifndef VISUAL_BASELINE_H
#define VISUAL_BASELINE_H

#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VB_REF_IMAGE_FILE "ch_baseline_ref.png"
#define VB_CAMERA_CONF_FILE "camera.conf"
/** CH 판정: 320×240 분석 프레임 ROI 안 빨간 LED 램프 (camera.conf roi 로 덮어씀) */
#define VB_ANALYSIS_W 320
#define VB_ANALYSIS_H 240
#define VB_ROI_DEFAULT_X 170
#define VB_ROI_DEFAULT_Y  90
#define VB_ROI_DEFAULT_W 150
#define VB_ROI_DEFAULT_H 150
/** 프로브 범위: PC마다 USB가 0번일 수도 있음(데스크톱) */
#define VB_CAMERA_PROBE_MIN_INDEX 0
#define VB_CAMERA_PROBE_MAX_INDEX 9
/** 호환 매크로(최소 인덱스). 내장 스킵용이 아님 */
#define VB_CAMERA_USB_FIRST_MIN_INDEX VB_CAMERA_PROBE_MIN_INDEX
/** camera.conf 미지정·자동: USB 장치명/경로 우선으로 첫 카메라 */
#define VB_CAMERA_INDEX_AUTO (-1)
/** score=1.0: ROI 안 빨간 LED 감지됨 → CH 기준점 일치 */
#define VB_MATCH_OK_THRESHOLD 0.85
/** 이보다 낮으면 카메라 기준으로 자동 보정 시도 */
#define VB_MATCH_TRIGGER_LOW 0.5
/** 연속 프레임 수 (200ms 간격 × 8 ≈ 1.6초) */
#define VB_STABLE_FRAMES 8

typedef struct VisualBaseline VisualBaseline;

VisualBaseline *vb_create(void);
void vb_destroy(VisualBaseline *vb);

/** mrwatchmaker.exe 와 같은 폴더의 ch_baseline_ref.png (없으면 NULL) */
char *vb_default_reference_path(void);

/** device_index: 0~VB_CAMERA_PROBE_MAX_INDEX */
int vb_open_camera(VisualBaseline *vb, int device_index);
/** USB(장치경로/이름) 우선 자동 선택. 없으면 사용 가능 카메라(IR 제외) */
int vb_open_camera_auto(VisualBaseline *vb);
void vb_close_camera(VisualBaseline *vb);
int vb_has_camera(const VisualBaseline *vb);
/** 열린 장치 번호(실패 시 -1) */
int vb_get_camera_device_index(const VisualBaseline *vb);
/** camera.conf 저장 인덱스(0~). 없거나 auto면 VB_CAMERA_INDEX_AUTO */
int vb_get_saved_camera_index(void);
/** 카메라 인덱스 저장. VB_CAMERA_INDEX_AUTO 이면 auto 로 기록 */
int vb_save_camera_index_to_conf(int device_index);
/** 사용 가능한(USB 우선) 카메라 인덱스 나열. 반환=개수 */
int vb_probe_usb_cameras(int *indices_out, int max_count);
/** 해당 인덱스 카메라의 Windows 표시 이름(UTF-8)을 buf 에. 성공 1, 실패 0 */
int vb_get_camera_name(int index, char *buf, int buflen);
int vb_has_reference(const VisualBaseline *vb);

/** exe 폴더 ch_baseline_ref.png 경로 (없어도 반환, 호출자 g_free) */
char *vb_reference_path_alloc(void);

int vb_load_reference(VisualBaseline *vb, const char *path);
int vb_save_reference_snapshot(VisualBaseline *vb, const char *path);

/** 카메라 프레임만 캡처(미리보기용, 기준 이미지 불필요). 실패 시 0 */
int vb_grab_preview(VisualBaseline *vb);

/** 마지막 캡처와 참조 비교. 기준 없으면 0 */
int vb_score_last_frame(VisualBaseline *vb, double *score_out);

/** 캡처 + 유사도(기준 있을 때). 실패 시 0 */
int vb_grab_and_score(VisualBaseline *vb, double *score_out);

/** 마지막 캡처 프레임 미리보기 (호출자 g_object_unref) */
GdkPixbuf *vb_copy_preview_pixbuf(VisualBaseline *vb);

#ifdef __cplusplus
}
#endif

#endif
