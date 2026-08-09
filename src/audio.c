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

#include "mrwatchmaker.h"
#include <portaudio.h>

float pa_buffers[2][PA_BUFF_SIZE];
int write_pointer = 0;
uint64_t timestamp = 0;
pthread_mutex_t audio_mutex;
static PaStream *g_stream = NULL;
static int g_preferred_input_device = -1; /* -1=auto */
static int g_active_input_device = -1;

static int str_contains_nocase(const char *s, const char *needle)
{
	if (!s || !needle)
		return 0;
	gchar *ls = g_ascii_strdown(s, -1);
	gchar *ln = g_ascii_strdown(needle, -1);
	int hit = (strstr(ls, ln) != NULL);
	g_free(ls);
	g_free(ln);
	return hit;
}

int audio_device_looks_usb(int pa_device_index)
{
	const PaDeviceInfo *di;
	const PaHostApiInfo *ha;
	if (pa_device_index < 0)
		return 0;
	di = Pa_GetDeviceInfo(pa_device_index);
	if (!di)
		return 0;
	if (str_contains_nocase(di->name, "usb"))
		return 1;
	ha = Pa_GetHostApiInfo(di->hostApi);
	if (ha && str_contains_nocase(ha->name, "usb"))
		return 1;
	return 0;
}

const char *audio_get_device_display_name(int pa_device_index)
{
	const PaDeviceInfo *di;
	if (pa_device_index < 0)
		return "Auto (USB first)";
	di = Pa_GetDeviceInfo(pa_device_index);
	if (!di || !di->name)
		return "Unknown device";
	return di->name;
}

int audio_get_input_device_count(void)
{
	int i, cnt = 0;
	int n = Pa_GetDeviceCount();
	if (n < 0)
		return 0;
	for (i = 0; i < n; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if (di && di->maxInputChannels > 0)
			cnt++;
	}
	return cnt;
}

int audio_get_nth_input_device_index(int nth)
{
	int i, k = 0;
	int n = Pa_GetDeviceCount();
	if (n < 0 || nth < 0)
		return -1;
	for (i = 0; i < n; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if (di && di->maxInputChannels > 0) {
			if (k == nth)
				return i;
			k++;
		}
	}
	return -1;
}

int audio_set_preferred_input_device(int pa_device_index)
{
	g_preferred_input_device = pa_device_index;
	return 0;
}

int audio_get_preferred_input_device(void)
{
	return g_preferred_input_device;
}

int audio_get_active_input_device(void)
{
	return g_active_input_device;
}

static int choose_input_device(void)
{
	int i;
	int preferred = g_preferred_input_device;
	int n = Pa_GetDeviceCount();
	if (n < 0)
		return paNoDevice;

	if (preferred >= 0) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(preferred);
		if (di && di->maxInputChannels > 0)
			return preferred;
	}

	/* auto: USB 입력 우선 */
	for (i = 0; i < n; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if (di && di->maxInputChannels > 0 && audio_device_looks_usb(i))
			return i;
	}

	return Pa_GetDefaultInputDevice();
}

static int paudio_callback(const void *input_buffer,
			   void *output_buffer,
			   unsigned long frame_count,
			   const PaStreamCallbackTimeInfo *time_info,
			   PaStreamCallbackFlags status_flags,
			   void *data)
{
	UNUSED(output_buffer);
	UNUSED(time_info);
	UNUSED(status_flags);
	unsigned long i;
	long channels = (long)data;
	int wp = write_pointer;
	for(i=0; i < frame_count; i++) {
		if(channels == 1) {
			pa_buffers[0][wp] = ((float *)input_buffer)[i];
			pa_buffers[1][wp] = ((float *)input_buffer)[i];
		} else {
			pa_buffers[0][wp] = ((float *)input_buffer)[2*i];
			pa_buffers[1][wp] = ((float *)input_buffer)[2*i + 1];
		}
		if(wp < PA_BUFF_SIZE - 1) wp++;
		else wp = 0;
	}
	pthread_mutex_lock(&audio_mutex);
	write_pointer = wp;
	timestamp += frame_count;
	pthread_mutex_unlock(&audio_mutex);
	return 0;
}

int start_portaudio(int *nominal_sample_rate, double *real_sample_rate)
{
	PaStreamParameters in;

	if(pthread_mutex_init(&audio_mutex,NULL)) {
		error("Failed to setup audio mutex");
		return 1;
	}

	PaError err = Pa_Initialize();
	if(err!=paNoError)
		goto error;

#ifdef DEBUG
	if(testing) {
		*nominal_sample_rate = PA_SAMPLE_RATE;
		*real_sample_rate = PA_SAMPLE_RATE;
		goto end;
	}
#endif

	PaDeviceIndex input_device = choose_input_device();
	if(input_device == paNoDevice) {
		error("No audio input device found");
		return 1;
	}
	const PaDeviceInfo *di = Pa_GetDeviceInfo(input_device);
	long channels = di ? di->maxInputChannels : 0;
	if(channels == 0) {
		error("Selected audio device has no input channels");
		return 1;
	}
	if(channels > 2) channels = 2;
	memset(&in, 0, sizeof(in));
	in.device = input_device;
	in.channelCount = channels;
	in.sampleFormat = paFloat32;
	in.suggestedLatency = di ? di->defaultLowInputLatency : 0.0;
	in.hostApiSpecificStreamInfo = NULL;
	err = Pa_OpenStream(&g_stream, &in, NULL, PA_SAMPLE_RATE,
		paFramesPerBufferUnspecified, paNoFlag, paudio_callback, (void*)channels);
	if(err!=paNoError)
		goto error;

	err = Pa_StartStream(g_stream);
	if(err!=paNoError)
		goto error;

	g_active_input_device = input_device;
	const PaStreamInfo *info = Pa_GetStreamInfo(g_stream);
	*nominal_sample_rate = PA_SAMPLE_RATE;
	*real_sample_rate = info->sampleRate;
#ifdef DEBUG
end:
#endif
	debug("sample rate: nominal = %d real = %f\n",*nominal_sample_rate,*real_sample_rate);
	debug("audio input device: #%d %s\n", g_active_input_device, audio_get_device_display_name(g_active_input_device));

	return 0;

error:
	if (g_stream) {
		Pa_CloseStream(g_stream);
		g_stream = NULL;
	}
	g_active_input_device = -1;
	error("Error opening audio input: %s", Pa_GetErrorText(err));
	return 1;
}

int terminate_portaudio()
{
	debug("Closing portaudio\n");
	if (g_stream) {
		Pa_StopStream(g_stream);
		Pa_CloseStream(g_stream);
		g_stream = NULL;
	}
	g_active_input_device = -1;
	PaError err = Pa_Terminate();
	if(err != paNoError) {
		error("Error closing audio: %s", Pa_GetErrorText(err));
		return 1;
	}
	return 0;
}

uint64_t get_timestamp(int light)
{
	pthread_mutex_lock(&audio_mutex);
	uint64_t ts = light ? timestamp / 2 : timestamp;
	pthread_mutex_unlock(&audio_mutex);
	return ts;
}

static void fill_buffers(struct processing_buffers *p, int light)
{
	pthread_mutex_lock(&audio_mutex);
	uint64_t ts = timestamp;
	int wp = write_pointer;
	pthread_mutex_unlock(&audio_mutex);
	if(wp < 0 || wp >= PA_BUFF_SIZE) wp = 0;
	if(light) {
		if(wp % 2) wp--;
		ts /= 2;
	}
	int i;
	for(i=0; i<NSTEPS; i++) {
		int j,k;
		p[i].timestamp = ts;
		if(light) k = wp - 2*p[i].sample_count;
		else k = wp - p[i].sample_count;

		if(k < 0) k += PA_BUFF_SIZE;
		for(j=0; j < p[i].sample_count; j++) {
			p[i].samples[j] = pa_buffers[0][k] + pa_buffers[1][k];
			k += light ? 2 : 1;
			if(k >= PA_BUFF_SIZE) k -= PA_BUFF_SIZE;
		}
	}
}

int analyze_pa_data(struct processing_data *pd, int bph, double la, uint64_t events_from)
{
	struct processing_buffers *p = pd->buffers;
	fill_buffers(p, pd->is_light);

	int i;
	debug("\nSTART OF COMPUTATION CYCLE\n\n");
	for(i=0; i<NSTEPS; i++) {
		p[i].last_tic = pd->last_tic;
		p[i].events_from = events_from;
		process(&p[i], bph, la, pd->is_light);
		if( !p[i].ready ) break;
		debug("step %d : %f +- %f\n",i,p[i].period/p[i].sample_rate,p[i].sigma/p[i].sample_rate);
	}
	if(i) {
		pd->last_tic = p[i-1].last_tic;
		debug("%f +- %f\n",p[i-1].period/p[i-1].sample_rate,p[i-1].sigma/p[i-1].sample_rate);
	} else
		debug("---\n");
	return i;
}

int analyze_pa_data_cal(struct processing_data *pd, struct calibration_data *cd)
{
	struct processing_buffers *p = pd->buffers;
	fill_buffers(p, pd->is_light);

	int i,j;
	debug("\nSTART OF CALIBRATION CYCLE\n\n");
	for(j=0; p[j].sample_count < 2*p[j].sample_rate; j++);
	for(i=0; i+j<NSTEPS-1; i++)
		if(test_cal(&p[i+j]))
			return i ? i+j : 0;
	if(process_cal(&p[NSTEPS-1], cd))
		return NSTEPS-1;
	return NSTEPS;
}
