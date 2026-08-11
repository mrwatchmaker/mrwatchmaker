/*
 * CH 카메라 기준점 — OpenCV UVC + ROI 내 빨간 LED 램프 감지
 */
#include "visual_baseline.h"



#include <opencv2/imgcodecs.hpp>

#include <opencv2/imgproc.hpp>

#include <opencv2/videoio.hpp>



#include <glib.h>

#include <glib/gstdio.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef G_OS_WIN32

#include <windows.h>

#include <dshow.h>

#include <oleauto.h>

#endif



#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <mutex>



struct VbRoi {

	int x;

	int y;

	int w;

	int h;

};



struct VisualBaseline {

	cv::VideoCapture cap;

	cv::Mat last_bgr;

	VbRoi roi;

	std::mutex mtx;

	int cam_open = 0;

	int ref_loaded = 0;

	int device_index = -1;

};



static void vb_list_dshow_camera_meta(std::vector<std::string> *names,
	std::vector<std::string> *paths);
static std::string vb_tolower_ascii(std::string s);
/** USB 장치경로에서 vid_xxxx&pid_yyyy 서명을 추출(소문자). 없으면 "" */
static std::string vb_extract_usbid(const std::string &device_path);

static gchar *vb_exe_dir_alloc(void)

{

#ifdef G_OS_WIN32

	gchar *dir = g_win32_get_package_installation_directory_of_module(NULL);

	if (dir)

		return dir;

#endif

	return g_strdup(".");

}



static gchar *vb_camera_conf_path_alloc(void)

{

	gchar *dir = vb_exe_dir_alloc();

	gchar *path = g_build_filename(dir, VB_CAMERA_CONF_FILE, NULL);

	g_free(dir);

	return path;

}



static void vb_roi_defaults(VbRoi *roi)

{

	if (!roi)

		return;

	roi->x = VB_ROI_DEFAULT_X;

	roi->y = VB_ROI_DEFAULT_Y;

	roi->w = VB_ROI_DEFAULT_W;

	roi->h = VB_ROI_DEFAULT_H;

}



/** camera.conf: usb VID&PID | camera N | auto | 예전 숫자 + 선택적 "roi x y w h" */
static void vb_read_camera_conf(int *cam_idx_out, VbRoi *roi_out, std::string *usbid_out)
{
	gchar *path = vb_camera_conf_path_alloc();
	FILE *f = fopen(path, "r");
	g_free(path);

	if (cam_idx_out)
		*cam_idx_out = VB_CAMERA_INDEX_AUTO;
	if (roi_out)
		vb_roi_defaults(roi_out);
	if (usbid_out)
		usbid_out->clear();
	if (!f)
		return;

	char line[96];
	while (fgets(line, sizeof(line), f)) {
		int n = -1;
		if (usbid_out) {
			char sig[64];
			if (sscanf(line, " usb %63s", sig) == 1)
				*usbid_out = vb_tolower_ascii(std::string(sig));
		}
		if (cam_idx_out) {
			if (g_ascii_strncasecmp(line, "auto", 4) == 0) {
				*cam_idx_out = VB_CAMERA_INDEX_AUTO;
			} else if (sscanf(line, " camera %d", &n) == 1
				   && n >= VB_CAMERA_PROBE_MIN_INDEX
				   && n <= VB_CAMERA_PROBE_MAX_INDEX) {
				*cam_idx_out = n;
			} else if (sscanf(line, " %d", &n) == 1) {
				/* 예전 포맷: 0=자동, 1~9=고정. 새 고정 0은 "camera 0" */
				if (n == 0)
					*cam_idx_out = VB_CAMERA_INDEX_AUTO;
				else if (n >= 1 && n <= VB_CAMERA_PROBE_MAX_INDEX)
					*cam_idx_out = n;
			}
		}

		if (roi_out) {
			int x, y, w, h;
			float fx, fy, fw, fh;
			if (sscanf(line, " roi %d %d %d %d", &x, &y, &w, &h) == 4) {
				roi_out->x = x;
				roi_out->y = y;
				roi_out->w = w;
				roi_out->h = h;
			} else if (sscanf(line, " roi %f %f %f %f", &fx, &fy, &fw, &fh) == 4) {
				roi_out->x = (int)(fx * VB_ANALYSIS_W);
				roi_out->y = (int)(fy * VB_ANALYSIS_H);
				roi_out->w = (int)(fw * VB_ANALYSIS_W);
				roi_out->h = (int)(fh * VB_ANALYSIS_H);
			}
		}
	}
	fclose(f);
}

int vb_get_saved_camera_index(void)
{
	int idx = VB_CAMERA_INDEX_AUTO;
	vb_read_camera_conf(&idx, NULL, NULL);
	return idx;
}

/** camera.conf 에 저장된 USB 하드웨어 서명(vid&pid). 없으면 "" */
static std::string vb_read_saved_usbid(void)
{
	std::string sig;
	vb_read_camera_conf(NULL, NULL, &sig);
	return sig;
}

int vb_save_camera_index_to_conf(int device_index)
{
	VbRoi roi;
	vb_read_camera_conf(NULL, &roi, NULL);

	/* 선택한 카메라의 USB 하드웨어 ID(VID/PID)도 함께 저장해서,
	 * 다음부터는 인덱스가 바뀌어도 같은 카메라를 정확히 찾게 한다. */
	std::string usbid;
	if (device_index >= VB_CAMERA_PROBE_MIN_INDEX
	    && device_index <= VB_CAMERA_PROBE_MAX_INDEX) {
		std::vector<std::string> names, paths;
		vb_list_dshow_camera_meta(&names, &paths);
		if ((size_t)device_index < paths.size())
			usbid = vb_extract_usbid(paths[(size_t)device_index]);
	}

	gchar *path = vb_camera_conf_path_alloc();
	FILE *f = fopen(path, "w");
	g_free(path);
	if (!f)
		return 0;

	if (!usbid.empty())
		fprintf(f, "usb %s\n", usbid.c_str());
	if (device_index >= VB_CAMERA_PROBE_MIN_INDEX
	    && device_index <= VB_CAMERA_PROBE_MAX_INDEX)
		fprintf(f, "camera %d\n", device_index);
	else
		fprintf(f, "auto\n");
	fprintf(f, "roi %d %d %d %d\n", roi.x, roi.y, roi.w, roi.h);
	fclose(f);
	return 1;
}

static int vb_frame_looks_valid(const cv::Mat &bgr)
{
	if (bgr.empty() || bgr.cols < 32 || bgr.rows < 24)
		return 0;
	cv::Scalar mean = cv::mean(bgr);
	double brightness = (mean[0] + mean[1] + mean[2]) / 3.0;
	return brightness > 4.0;
}

static int vb_probe_open_index(int device_index)
{
	cv::VideoCapture *cap = NULL;
	int ok = 0;
	try {
		cap = new cv::VideoCapture();
		/* 없는 인덱스의 CAP_ANY 폴백은 qcap.dll 크래시를 유발할 수 있어 DSHOW만 사용 */
		if (!cap->open(device_index, cv::CAP_DSHOW)) {
			delete cap;
			return 0;
		}
		cap->set(cv::CAP_PROP_FRAME_WIDTH, 640);
		cap->set(cv::CAP_PROP_FRAME_HEIGHT, 480);
		cv::Mat bgr;
		for (int i = 0; i < 6; i++) {
			if (cap->read(bgr) && vb_frame_looks_valid(bgr)) {
				ok = 1;
				break;
			}
		}
		if (!ok)
			ok = cap->read(bgr) && !bgr.empty();
		cap->release();
		delete cap;
		cap = NULL;
	} catch (...) {
		if (cap) {
			try { cap->release(); } catch (...) {}
			delete cap;
		}
		ok = 0;
	}
	return ok;
}

static std::string vb_tolower_ascii(std::string s)
{
	for (char &c : s) {
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	}
	return s;
}

static std::string vb_extract_usbid(const std::string &device_path)
{
	std::string p = vb_tolower_ascii(device_path);
	size_t v = p.find("vid_");
	if (v == std::string::npos)
		return std::string();
	size_t pid = p.find("pid_", v);
	if (pid == std::string::npos)
		return std::string();
	if (v + 8 > p.size())
		return std::string();
	std::string vid = p.substr(v, 8); /* vid_xxxx */

	size_t end = pid + 4;
	size_t hexn = 0;
	while (end < p.size() && hexn < 4
	       && ((p[end] >= '0' && p[end] <= '9')
		   || (p[end] >= 'a' && p[end] <= 'f'))) {
		end++;
		hexn++;
	}
	if (hexn == 0)
		return std::string();
	std::string pidpart = p.substr(pid, end - pid); /* pid_yyyy */
	return vid + "&" + pidpart;
}

/** 높을수록 USB 외장 가능성. IR은 제외, 노트북 내장은 낮은 점수 */
static int vb_camera_name_score(const std::string &friendly, const std::string &device_path)
{
	std::string name = vb_tolower_ascii(friendly);
	std::string path = vb_tolower_ascii(device_path);

	/* AiTimeBot 장비에 장착된 카메라 모듈(하드웨어 고정).
	 * 노트북 내장 카메라도 내부적으로 USB(UVC)라서 이름/USB 여부만으로는 구분이 안 된다.
	 * 그래서 VID/PID 로 못박아 최우선 선택한다. 어느 PC에 꽂아도 이 카메라만 잡힌다.
	 * (다른 AiTimeBot 카메라 모듈이 추가되면 아래 목록에 VID/PID 를 넣으면 된다.)
	 * FHD Camera = VID_1BCF&PID_2286. camera.conf 의 "usb ..." 로도 덮어쓸 수 있다. */
	static const char *kAiTimeBotUsbIds[] = {
		"vid_1bcf&pid_2286",
	};
	for (size_t i = 0; i < sizeof(kAiTimeBotUsbIds) / sizeof(kAiTimeBotUsbIds[0]); i++) {
		if (path.find(kAiTimeBotUsbIds[i]) != std::string::npos)
			return 400;
	}

	/* 이름에 AiTimeBot 이 들어가는 경우도 최우선 */
	if (name.find("aitimebot") != std::string::npos
	    || path.find("aitimebot") != std::string::npos)
		return 300;

	if (name.find("infrared") != std::string::npos
	    || name.find(" ir ") != std::string::npos
	    || name.find("ir camera") != std::string::npos
	    || name.rfind("ir ", 0) == 0
	    || name.find("tof") != std::string::npos
	    || name.find("windows hello") != std::string::npos)
		return -100;

	const int is_builtin =
		name.find("integrated") != std::string::npos
		|| name.find("built-in") != std::string::npos
		|| name.find("builtin") != std::string::npos
		|| name.find("facetime") != std::string::npos
		|| name.find("front camera") != std::string::npos
		|| name.find("user facing") != std::string::npos
		|| name.find("rgb camera") != std::string::npos;

	const int path_usb =
		path.find("\\usb#") != std::string::npos
		|| path.find("#usb") != std::string::npos
		|| path.find("usb#") != std::string::npos
		|| path.find("\\usb") != std::string::npos;

	const int name_usb =
		name.find("usb") != std::string::npos
		|| name.find("uvc") != std::string::npos
		|| name.find("external") != std::string::npos;

	if (is_builtin)
		return name_usb ? 30 : 15;

	if (path_usb || name_usb)
		return 200;

	return 40;
}

#ifdef G_OS_WIN32
static std::string vb_bstr_to_utf8(BSTR bstr)
{
	if (!bstr)
		return std::string();
	int wlen = (int)SysStringLen(bstr);
	if (wlen <= 0)
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, bstr, wlen, NULL, 0, NULL, NULL);
	if (n <= 0)
		return std::string();
	std::string out(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, bstr, wlen, &out[0], n, NULL, NULL);
	return out;
}

/** DirectShow 열거 순서 ≈ OpenCV CAP_DSHOW 인덱스. 실제 장치만 나열. */
static void vb_list_dshow_camera_meta(std::vector<std::string> *names,
	std::vector<std::string> *paths)
{
	if (names)
		names->clear();
	if (paths)
		paths->clear();

	HRESULT hr_co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	const int do_uninit = (hr_co == S_OK);

	ICreateDevEnum *dev_enum = NULL;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void **)&dev_enum);
	if (FAILED(hr) || !dev_enum) {
		if (do_uninit)
			CoUninitialize();
		return;
	}

	IEnumMoniker *enum_mon = NULL;
	hr = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_mon, 0);
	if (hr != S_OK || !enum_mon) {
		dev_enum->Release();
		if (do_uninit)
			CoUninitialize();
		return;
	}

	IMoniker *mon = NULL;
	while (enum_mon->Next(1, &mon, NULL) == S_OK) {
		std::string friendly;
		std::string path;
		IPropertyBag *prop = NULL;
		if (SUCCEEDED(mon->BindToStorage(0, 0, IID_IPropertyBag, (void **)&prop)) && prop) {
			VARIANT var;
			VariantInit(&var);
			if (SUCCEEDED(prop->Read(L"FriendlyName", &var, 0)) && var.vt == VT_BSTR)
				friendly = vb_bstr_to_utf8(var.bstrVal);
			VariantClear(&var);
			VariantInit(&var);
			if (SUCCEEDED(prop->Read(L"DevicePath", &var, 0)) && var.vt == VT_BSTR)
				path = vb_bstr_to_utf8(var.bstrVal);
			VariantClear(&var);
			prop->Release();
		}
		if (names)
			names->push_back(friendly);
		if (paths)
			paths->push_back(path);
		mon->Release();
		mon = NULL;
	}

	enum_mon->Release();
	dev_enum->Release();
	if (do_uninit)
		CoUninitialize();
}
#else
static void vb_list_dshow_camera_meta(std::vector<std::string> *names,
	std::vector<std::string> *paths)
{
	if (names)
		names->clear();
	if (paths)
		paths->clear();
}
#endif

struct VbCamCand {
	int index;
	int score;
};

static int vb_score_for_index(int index,
	const std::vector<std::string> &names,
	const std::vector<std::string> &paths)
{
	std::string name;
	std::string path;
	if (index >= 0 && (size_t)index < names.size())
		name = names[(size_t)index];
	if (index >= 0 && (size_t)index < paths.size())
		path = paths[(size_t)index];
	return vb_camera_name_score(name, path);
}

static void vb_sort_candidates(std::vector<VbCamCand> *out)
{
	std::sort(out->begin(), out->end(),
		[](const VbCamCand &a, const VbCamCand &b) {
			if (a.score != b.score)
				return a.score > b.score;
			return a.index < b.index;
		});
}

/**
 * DirectShow에 실제로 있는 장치만 후보로 사용.
 * 없는 인덱스까지 CAP_DSHOW로 열면 일부 PC에서 qcap.dll 접근위반 크래시.
 */
static void vb_collect_candidates(std::vector<VbCamCand> *out, int usb_only_menu, int open_verify)
{
	out->clear();
	std::vector<std::string> names, paths;
	vb_list_dshow_camera_meta(&names, &paths);

	int max_i;
	if (!names.empty()) {
		max_i = (int)names.size() - 1;
		if (max_i > VB_CAMERA_PROBE_MAX_INDEX)
			max_i = VB_CAMERA_PROBE_MAX_INDEX;
	} else {
		max_i = 3; /* 메타 실패 시에만 0~3 제한 시도 */
	}

	int any_ext_usb = 0;
	if (usb_only_menu && !names.empty()) {
		for (size_t k = 0; k < names.size(); k++) {
			if (vb_camera_name_score(names[k],
				k < paths.size() ? paths[k] : std::string()) >= 100)
				any_ext_usb = 1;
		}
	}

	for (int i = VB_CAMERA_PROBE_MIN_INDEX; i <= max_i; i++) {
		int score = names.empty()
			? (i >= 1 ? 45 : 40)
			: vb_score_for_index(i, names, paths);
		if (score <= -50)
			continue;
		if (usb_only_menu && any_ext_usb && score < 100)
			continue;
		if (open_verify && !vb_probe_open_index(i))
			continue;
		VbCamCand c;
		c.index = i;
		c.score = score;
		out->push_back(c);
	}

	vb_sort_candidates(out);
}

int vb_probe_usb_cameras(int *indices_out, int max_count)
{
	std::vector<VbCamCand> cands;
	vb_collect_candidates(&cands, 1, 0);
	std::vector<std::string> names, paths;
	vb_list_dshow_camera_meta(&names, &paths);
	int n = 0;
	for (size_t i = 0; i < cands.size() && n < max_count; i++) {
		/* 메타 열거가 됐다면 USB(외장) 카메라만 목록에 넣는다. 내장 폴백 없음 */
		if (!names.empty()
		    && vb_score_for_index(cands[i].index, names, paths) < 100)
			continue;
		if (indices_out)
			indices_out[n] = cands[i].index;
		n++;
	}
	return n;
}

int vb_get_camera_name(int index, char *buf, int buflen)
{
	if (!buf || buflen <= 0)
		return 0;
	buf[0] = '\0';
	std::vector<std::string> names, paths;
	vb_list_dshow_camera_meta(&names, &paths);
	if (index < 0 || (size_t)index >= names.size() || names[(size_t)index].empty())
		return 0;
	g_strlcpy(buf, names[(size_t)index].c_str(), (gsize)buflen);
	return 1;
}

static cv::Mat vb_bgr_to_analysis(const cv::Mat &bgr)
{
	cv::Mat out;
	cv::resize(bgr, out, cv::Size(VB_ANALYSIS_W, VB_ANALYSIS_H));
	return out;
}

static void vb_roi_clamp(VbRoi *roi)
{
	if (!roi || roi->w <= 8 || roi->h <= 8)
		vb_roi_defaults(roi);
	roi->x = std::max(0, std::min(roi->x, VB_ANALYSIS_W - 16));
	roi->y = std::max(0, std::min(roi->y, VB_ANALYSIS_H - 16));
	roi->w = std::max(16, std::min(roi->w, VB_ANALYSIS_W - roi->x));
	roi->h = std::max(16, std::min(roi->h, VB_ANALYSIS_H - roi->y));
}

/** ROI 안에서 빨간 LED 램프 blob 탐색. score=1.0 이면 기준점 일치 */
static double vb_detect_red_led_score(const cv::Mat &bgr, const VbRoi &roi,
	int *cx_out, int *cy_out, int *radius_out)
{
	VbRoi r = roi;
	vb_roi_clamp(&r);
	cv::Rect rect(r.x, r.y, r.w, r.h);
	if (rect.x + rect.width > bgr.cols || rect.y + rect.height > bgr.rows)
		return 0.0;

	cv::Mat patch = bgr(rect);
	cv::Mat hsv;
	cv::cvtColor(patch, hsv, cv::COLOR_BGR2HSV);

	cv::Mat m1, m2, mask;
	cv::inRange(hsv, cv::Scalar(0, 120, 90), cv::Scalar(12, 255, 255), m1);
	cv::inRange(hsv, cv::Scalar(160, 120, 90), cv::Scalar(180, 255, 255), m2);
	cv::bitwise_or(m1, m2, mask);

	/* BGR에서도 R가 G·B보다 충분히 큰 픽셀만 (주변 반사 제거) */
	cv::Mat ch[3];
	cv::split(patch, ch);
	cv::Mat rg, rb;
	cv::compare(ch[2], ch[1] + 35, rg, cv::CMP_GT);
	cv::compare(ch[2], ch[0] + 35, rb, cv::CMP_GT);
	cv::bitwise_and(mask, rg, mask);
	cv::bitwise_and(mask, rb, mask);

	cv::Mat kern = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
	cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kern);
	cv::dilate(mask, mask, kern);

	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

	double best_area = 0.0;
	cv::Point best_center(-1, -1);
	const double roi_area = (double)r.w * (double)r.h;

	for (const auto &c : contours) {
		double area = cv::contourArea(c);
		if (area < 3.0 || area > roi_area * 0.20)
			continue;
		cv::Moments m = cv::moments(c);
		if (m.m00 <= 0.0)
			continue;
		if (area > best_area) {
			best_area = area;
			best_center = cv::Point((int)(m.m10 / m.m00), (int)(m.m01 / m.m00));
		}
	}

	if (best_area < 3.0 || best_center.x < 0)
		return 0.0;

	if (cx_out)
		*cx_out = r.x + best_center.x;
	if (cy_out)
		*cy_out = r.y + best_center.y;
	if (radius_out)
		*radius_out = std::max(3, (int)sqrt(best_area / 3.14159) + 2);

	return 1.0;
}



static int vb_try_open_index(VisualBaseline *vb, int device_index)
{
	if (!vb)
		return 0;
	try {
		vb->cap.release();
		if (!vb->cap.open(device_index, cv::CAP_DSHOW))
			return 0;
		vb->cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
		vb->cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
		cv::Mat bgr;
		for (int i = 0; i < 10; i++) {
			if (vb->cap.read(bgr) && vb_frame_looks_valid(bgr))
				return 1;
		}
		return vb->cap.read(bgr) && !bgr.empty();
	} catch (...) {
		try { vb->cap.release(); } catch (...) {}
		return 0;
	}
}

extern "C"
 {



VisualBaseline *vb_create(void)

{

	VisualBaseline *vb = new VisualBaseline();

	vb_read_camera_conf(NULL, &vb->roi, NULL);

	return vb;

}



void vb_destroy(VisualBaseline *vb)

{

	delete vb;

}



char *vb_reference_path_alloc(void)

{

#ifdef G_OS_WIN32

	gchar *dir = g_win32_get_package_installation_directory_of_module(NULL);

	if (dir) {

		gchar *p = g_build_filename(dir, VB_REF_IMAGE_FILE, NULL);

		g_free(dir);

		return p;

	}

#endif

	return g_build_filename(".", VB_REF_IMAGE_FILE, NULL);

}



char *vb_default_reference_path(void)

{

	gchar *p = vb_reference_path_alloc();

	if (p && g_file_test(p, G_FILE_TEST_IS_REGULAR))

		return p;

	g_free(p);

	return NULL;

}



int vb_open_camera(VisualBaseline *vb, int device_index)
{
	if (!vb || device_index < VB_CAMERA_PROBE_MIN_INDEX
	    || device_index > VB_CAMERA_PROBE_MAX_INDEX)
		return 0;

	std::lock_guard<std::mutex> lk(vb->mtx);

	if (!vb_try_open_index(vb, device_index)) {
		vb->cap.release();
		vb->cam_open = 0;
		vb->device_index = -1;
		return 0;
	}

	for (int i = 0; i < 6; i++) {
		cv::Mat tmp;
		vb->cap.read(tmp);
	}

	vb->device_index = device_index;
	vb->cam_open = 1;
	return 1;
}

/** 해당 인덱스가 실제 USB(외장) 카메라인지. 메타 열거 실패 시 판단 불가(0) */
static int vb_index_is_usb_camera(int index)
{
	std::vector<std::string> names, paths;
	vb_list_dshow_camera_meta(&names, &paths);
	if (names.empty())
		return 0;
	return vb_score_for_index(index, names, paths) >= 100;
}

int vb_open_camera_auto(VisualBaseline *vb)
{
	if (!vb)
		return 0;

	/* 하드웨어는 항상 동일: USB(외장) 카메라만 사용, 내장/IR 카메라는 제외. */
	std::vector<std::string> names, paths;
	vb_list_dshow_camera_meta(&names, &paths);

	/* 1) 저장된 USB 하드웨어 ID(VID/PID)와 일치하는 카메라를 최우선으로.
	 *    인덱스가 바뀌거나 다른 PC여도 같은 카메라를 정확히 찾는다. */
	std::string want = vb_read_saved_usbid();
	if (!want.empty()) {
		for (size_t i = 0; i < paths.size(); i++) {
			if (vb_extract_usbid(paths[i]) != want)
				continue;
			std::string nm = i < names.size() ? names[i] : std::string();
			if (vb_camera_name_score(nm, paths[i]) <= -50)
				continue; /* IR 등 제외 */
			if (vb_open_camera(vb, (int)i))
				return 1;
		}
	}

	/* 2) 저장된 인덱스는 이 PC에서 실제 USB 카메라를 가리킬 때만 신뢰.
	 *    (다른 PC에서 만든 camera.conf가 여기선 내장 카메라를 가리킬 수 있음) */
	int conf_idx = vb_get_saved_camera_index();
	if (conf_idx >= VB_CAMERA_PROBE_MIN_INDEX
	    && conf_idx <= VB_CAMERA_PROBE_MAX_INDEX
	    && vb_index_is_usb_camera(conf_idx)
	    && vb_open_camera(vb, conf_idx))
		return 1;

	/* 3) 첫 USB(외장) 카메라. 전체 선탐침 금지: 메타로 후보만 만들고 하나씩 연다. */
	std::vector<VbCamCand> cands;
	vb_collect_candidates(&cands, 1, 0);
	for (size_t i = 0; i < cands.size(); i++) {
		/* 메타 열거가 됐다면 USB(외장) 카메라만 허용 */
		if (!names.empty()
		    && vb_score_for_index(cands[i].index, names, paths) < 100)
			continue;
		if (vb_open_camera(vb, cands[i].index))
			return 1;
	}
	return 0;
}

int vb_get_camera_device_index(const VisualBaseline *vb)

{

	if (!vb || !vb->cam_open)

		return -1;

	return vb->device_index;

}



void vb_close_camera(VisualBaseline *vb)

{

	if (!vb)

		return;

	std::lock_guard<std::mutex> lk(vb->mtx);

	vb->cap.release();

	vb->cam_open = 0;

	vb->device_index = -1;

}



int vb_has_camera(const VisualBaseline *vb)

{

	return vb && vb->cam_open;

}



int vb_has_reference(const VisualBaseline *vb)
{
	/* LED 모드: ch_baseline_ref.png 없어도 ROI 안 빨간 램프만으로 판정 가능 */
	return vb != NULL;
}



static int load_ref_roi(VisualBaseline *vb, const char *path)
{
	cv::Mat bgr = cv::imread(path, cv::IMREAD_COLOR);
	if (bgr.empty())
		return 0;
	vb_read_camera_conf(NULL, &vb->roi, NULL);
	vb_roi_clamp(&vb->roi);
	vb->ref_loaded = 1;
	return 1;
}



int vb_load_reference(VisualBaseline *vb, const char *path)

{

	if (!vb || !path)

		return 0;

	std::lock_guard<std::mutex> lk(vb->mtx);

	return load_ref_roi(vb, path);

}



int vb_save_reference_snapshot(VisualBaseline *vb, const char *path)

{

	if (!vb || !path)

		return 0;

	cv::Mat bgr;

	{

		std::lock_guard<std::mutex> lk(vb->mtx);

		if (!vb->cam_open)

			return 0;

		if (!vb->cap.read(bgr) || bgr.empty())

			return 0;

		vb->last_bgr = bgr.clone();

	}

	if (!cv::imwrite(path, bgr))

		return 0;

	return load_ref_roi(vb, path);

}



static void pixbuf_pixels_destroy(guchar *pixels, gpointer /*data*/)

{

	g_free(pixels);

}



static GdkPixbuf *mat_to_pixbuf(const cv::Mat &bgr)

{

	if (bgr.empty())

		return NULL;

	cv::Mat rgb;

	cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

	const int w = rgb.cols;

	const int h = rgb.rows;

	const int rowstride = w * 3;

	guchar *data = (guchar *)g_malloc((gsize)(rowstride * h));

	if (!data)

		return NULL;

	for (int y = 0; y < h; y++)

		memcpy(data + y * rowstride, rgb.ptr(y), (size_t)rowstride);

	return gdk_pixbuf_new_from_data(

		data, GDK_COLORSPACE_RGB, FALSE, 8, w, h, rowstride,

		pixbuf_pixels_destroy, NULL);

}



int vb_grab_preview(VisualBaseline *vb)

{

	if (!vb)

		return 0;

	std::lock_guard<std::mutex> lk(vb->mtx);

	if (!vb->cam_open)

		return 0;

	cv::Mat bgr;

	if (!vb->cap.read(bgr) || bgr.empty())

		return 0;

	vb->last_bgr = bgr.clone();

	return 1;

}



int vb_score_last_frame(VisualBaseline *vb, double *score_out)
{
	if (!vb || vb->last_bgr.empty())
		return 0;

	cv::Mat analysis = vb_bgr_to_analysis(vb->last_bgr);
	double score = vb_detect_red_led_score(analysis, vb->roi, NULL, NULL, NULL);
	if (score_out)
		*score_out = score;
	return 1;
}



int vb_grab_and_score(VisualBaseline *vb, double *score_out)

{

	if (!vb_grab_preview(vb))
		return 0;

	if (score_out)
		*score_out = 0.0;
	return vb_score_last_frame(vb, score_out);

}



GdkPixbuf *vb_copy_preview_pixbuf(VisualBaseline *vb)

{

	if (!vb)

		return NULL;

	std::lock_guard<std::mutex> lk(vb->mtx);

	if (vb->last_bgr.empty())

		return NULL;

	cv::Mat out = vb->last_bgr.clone();
	cv::Mat analysis = vb_bgr_to_analysis(out);

	VbRoi r = vb->roi;
	vb_roi_clamp(&r);
	double sx = (double)out.cols / VB_ANALYSIS_W;
	double sy = (double)out.rows / VB_ANALYSIS_H;

	cv::Rect roi_rect((int)(r.x * sx), (int)(r.y * sy),
		(int)(r.w * sx), (int)(r.h * sy));

	if (roi_rect.x >= 0 && roi_rect.y >= 0
	    && roi_rect.x + roi_rect.width <= out.cols
	    && roi_rect.y + roi_rect.height <= out.rows)
		cv::rectangle(out, roi_rect, cv::Scalar(0, 220, 0), 2);

	int led_cx = 0, led_cy = 0, led_r = 0;
	if (vb_detect_red_led_score(analysis, vb->roi, &led_cx, &led_cy, &led_r) >= VB_MATCH_OK_THRESHOLD) {
		cv::Point center((int)(led_cx * sx), (int)(led_cy * sy));
		int radius = std::max(4, (int)(led_r * (sx + sy) * 0.5));
		cv::circle(out, center, radius, cv::Scalar(0, 0, 255), 2);
	}

	return mat_to_pixbuf(out);

}



} /* extern "C" */

