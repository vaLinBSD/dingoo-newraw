#ifdef USE_DINGOO

#include <dingoo/slcd.h>
#include <dingoo/keyboard.h>
#include <dingoo/time.h>
#include <dingoo/audio.h>
#include <dingoo/cache.h>
#include <time.h>

#include "systemstub.h"
#include "util.h"

void* g_pGameDecodeBuf = NULL;

struct DingooStub : public SystemStub
{
	DingooStub() : _offscreen(NULL) {}
	virtual ~DingooStub() {}
	virtual void init(const char *title);
	virtual void destroy() {}
	virtual void setPalette(uint8 s, uint8 n, const uint8 *buf);
	virtual void copyRect(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *buf, uint32 pitch);
	virtual void processEvents();
	virtual void sleep(uint32 duration) {}
	virtual void delta(uint32 time_delta);
	virtual uint32 getTimeStamp();
	virtual void startAudio(AudioCallback callback, void *param);
	virtual void stopAudio();
	virtual uint32 getOutputSampleRate() { return SOUND_SAMPLE_RATE; }
	virtual void* addTimer(uint32 delaclocky, TimerCallback callback, void *param);
	virtual void removeTimer(void *timerId);
	virtual void* createMutex() { return NULL; }
	virtual void destroyMutex(void *mutex) {}
	virtual void lockMutex(void *mutex) {}
	virtual void unlockMutex(void *mutex) {}

	virtual int seed() const { return OSTimeGet(); }

	void checkTimers();
	void checkSound(uint32 time_delta);
	void checkKeys();

	enum { SCREEN_W = 320, SCREEN_H = 240, SOUND_SAMPLE_RATE = 22050 };

	uint8* _offscreen;
	uint16 _pal[16];
	struct eTimer
	{
		eTimer() { clear(); }
		void			clear() { activate_time = 0; callback = NULL; delay = 0; param = NULL; }
		uint32			activate_time;
		TimerCallback	callback;
		uint32			delay;
		void*			param;
	};
	eTimer timers[32];

	struct eAudio
	{
		eAudio() : handle(NULL), callback(NULL), param(NULL) {}
		bool Valid() const { return handle && callback; }
		void*			handle;
		AudioCallback	callback;
		void*			param;
	} audio;

	uint32 time_start;
	uint32 time_passed;
	uint32 time_game;
	int volume;
};

SystemStub *SystemStub_Dingoo_create() {
	return new DingooStub;
}

void DingooStub::init(const char *title)
{
	memset(&_pi, 0, sizeof(_pi));
	_offscreen = (uint8*)_lcd_get_frame();
	if(!_offscreen)
	{
		error("Unable to get offscreen buffer");
	}
	memset(_offscreen, 0, SCREEN_W*SCREEN_H*2);
	time_start = clock();
	time_passed = 0;
	time_game = 0;
	volume = 15;
}

inline uint16 BGR565(uint8 r, uint8 g, uint8 b) { return (((r&~7) << 8)|((g&~3) << 3)|(b >> 3)); }
void DingooStub::setPalette(uint8 s, uint8 n, const uint8 *buf)
{
	assert(s + n <= 16);
	for (int i = s; i < s + n; ++i)
	{
		uint8 c[3];
		for (int j = 0; j < 3; ++j)
		{
			uint8 col = buf[i * 3 + j];
			c[j] =  (col << 2) | (col & 3);
		}
		_pal[i] = BGR565(c[0], c[1], c[2]);
	}
}

void DingooStub::copyRect(uint16 x, uint16 y, uint16 w, uint16 h, const uint8 *buf, uint32 pitch)
{
	buf += y * pitch + x;
	uint16 *p = (uint16 *)_offscreen + (SCREEN_H - h)/2*SCREEN_W;
	while(h--)
	{
		for(int i = 0; i < w / 2; ++i)
		{
			uint8 p1 = *(buf + i) >> 4;
			uint8 p2 = *(buf + i) & 0xF;
			*(p + i * 2 + 0) = _pal[p1];
			*(p + i * 2 + 1) = _pal[p2];
		}
		p += SCREEN_W;
		buf += pitch;
	}
	__dcache_writeback_all();
	_lcd_set_frame();
}

enum eKeyBit
{
	K_POWER			= 1 << 7,
	K_BUTTON_A		= 1 << 31,
	K_BUTTON_B		= 1 << 21,
	K_BUTTON_X		= 1 << 16,
	K_BUTTON_Y      = 1 << 6,
	K_BUTTON_START	= 1 << 11,
	K_BUTTON_SELECT	= 1 << 10,
	K_TRIGGER_LEFT	= 1 << 8,
	K_TRIGGER_RIGHT	= 1 << 29,
	K_DPAD_UP		= 1 << 20,
	K_DPAD_DOWN		= 1 << 27,
	K_DPAD_LEFT		= 1 << 28,
	K_DPAD_RIGHT	= 1 << 18
};

void DingooStub::processEvents()
{
	checkTimers();
	checkKeys();
}
void DingooStub::checkSound(uint32 time_delta)
{
	if(!audio.Valid())
		return;
	if(time_delta)
	{
		int8 sound_buf[16384];
		uint32 sound_buf_len = time_delta*SOUND_SAMPLE_RATE/1000;
		if(sound_buf_len > 16384)
			sound_buf_len = 16384;
		if(sound_buf_len > 0)
		{
			audio.callback(audio.param, (uint8*)sound_buf, sound_buf_len);
			int8* sound_buf_end = sound_buf + sound_buf_len;
			int16 sound_buf2[16384];
			int16* b2 = sound_buf2;
			for(int8* b = sound_buf; b < sound_buf_end; ++b, ++b2)
			{
				int v = *b;
				v *= 256;
				*b2 = v;
			}
			static uint32 vol = 0;
			if(vol != volume)
			{
				vol = volume;
				waveout_set_volume(volume);
			}
			waveout_write(audio.handle, (char*)sound_buf2, sound_buf_len*2);
		}
	}
}
void DingooStub::checkKeys()
{
	_pi.dirMask = 0;
	_pi.button = false;
	if(_sys_judge_event(NULL) < 0)
	{
		_pi.quit = true;
		return;
	}

	KEY_STATUS ks;
	_kbd_get_status(&ks);

	if(ks.status&K_DPAD_LEFT)
		_pi.dirMask |= PlayerInput::DIR_LEFT;
	if(ks.status&K_DPAD_RIGHT)
		_pi.dirMask |= PlayerInput::DIR_RIGHT;
	if(ks.status&K_DPAD_UP)
		_pi.dirMask |= PlayerInput::DIR_UP;
	if(ks.status&K_DPAD_DOWN)
		_pi.dirMask |= PlayerInput::DIR_DOWN;

	if(ks.status&K_BUTTON_A)
		_pi.button = true;
	if(ks.status&K_BUTTON_SELECT)
		_pi.code = true;

	static bool bs = false;
	if(ks.status&K_BUTTON_START)
	{
		if(!bs)
			_pi.pause = true;
		bs = true;
	}
	else
		bs = false;

	if(ks.status&K_BUTTON_SELECT && ks.status&K_BUTTON_START)
		_pi.quit = true;

	static bool tl = false, tr = false;
	if(ks.status&K_TRIGGER_LEFT)
	{
		if(!tl)
		{
			volume -= 5;
			if(volume < 0)
				volume = 0;
		}
		tl = true;
	}
	else
		tl = false;
	if(ks.status&K_TRIGGER_RIGHT)
	{
		if(!tr)
		{
			volume += 5;
			if(volume > 30)
				volume = 30;
		}
		tr = true;
	}
	else
		tr = false;
}

void DingooStub::delta(uint32 time_delta)
{
	time_game += time_delta;
	checkSound(time_delta);
	do
	{
		checkTimers();
	}
	while(getTimeStamp() < time_game);
}

uint32 DingooStub::getTimeStamp()
{
	uint32 time_current = clock();
	uint32 time_d = time_current - time_start;
	if(time_d > CLOCKS_PER_SEC)
	{
		time_d -= CLOCKS_PER_SEC;
		time_start += CLOCKS_PER_SEC;
		time_passed += 1000;
	}
	return time_d*1000/CLOCKS_PER_SEC + time_passed;
}

void DingooStub::startAudio(AudioCallback callback, void *param)
{
	waveout_args wo = { SOUND_SAMPLE_RATE, 16, 1, volume };
	audio.handle = waveout_open(&wo);
	audio.callback = callback;
	audio.param = param;
}

void DingooStub::stopAudio()
{
	waveout_close(audio.handle);
	audio.handle = NULL;
}
void* DingooStub::addTimer(uint32 delay, TimerCallback callback, void *param)
{
	eTimer* t = timers;
	while(t->callback)
		++t;
	t->activate_time = getTimeStamp() + delay;
	t->callback = callback;
	t->delay = delay;
	t->param = param;
	return t;
}

void DingooStub::removeTimer(void* timerId)
{
	eTimer* t = (eTimer*)timerId;
	for(eTimer* tn = t + 1; tn->callback;)
	{
		*t++ = *tn++;
	}
	t->clear();
}
void DingooStub::checkTimers()
{
	uint32 time_current = getTimeStamp();
	for(eTimer* t = timers; t->callback;)
	{
		if(time_current > t->activate_time)
		{
			if(t->callback(t->delay, t->param))
			{
				t->activate_time += t->delay;
			}
			else
			{
				removeTimer(t);
				--t;
			}
		}
		++t;
	}
}

#endif//USE_DINGOO
