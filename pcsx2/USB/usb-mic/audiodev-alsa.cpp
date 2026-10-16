// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "USB/usb-mic/audiodev.h"
#include "retro_messager.h"

#include <alsa/asoundlib.h>

#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>

#include <climits>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	std::string UsbDeviceOf(const std::string& sysfs_link)
	{
		char real[PATH_MAX];
		if (!realpath(sysfs_link.c_str(), real))
			return {};
		// The link names a USB interface; its parent is the device.
		return dirname(real);
	}

	bool Exists(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0;
	}

	// The microphone on the same USB device as a camera, else any USB microphone.
	std::string FindCaptureDevice(const std::string& sys, const std::string& proc)
	{
		std::vector<std::string> cameras;
		if (DIR* d = opendir((sys + "/class/video4linux").c_str()))
		{
			while (dirent* e = readdir(d))
			{
				if (e->d_name[0] == '.')
					continue;
				const std::string usb = UsbDeviceOf(sys + "/class/video4linux/" + e->d_name + "/device");
				if (!usb.empty())
					cameras.push_back(usb);
			}
			closedir(d);
		}

		std::string usb_microphone;
		for (int card = 0; card < 32; card++)
		{
			int pcm = 0;
			while (pcm < 32 && !Exists(proc + "/asound/card" + std::to_string(card) + "/pcm" + std::to_string(pcm) + "c"))
				pcm++;
			if (pcm == 32)
				continue;

			const std::string link = sys + "/class/sound/card" + std::to_string(card) + "/device";
			char real[PATH_MAX];
			if (!realpath(link.c_str(), real))
				continue;
			const std::string name = "plughw:" + std::to_string(card) + "," + std::to_string(pcm);
			const std::string usb = UsbDeviceOf(link);
			for (const std::string& camera : cameras)
			{
				if (camera == usb)
					return name;
			}
			if (usb_microphone.empty() && std::string(real).find("/usb") != std::string::npos)
				usb_microphone = name;
		}
		return usb_microphone;
	}

	class AlsaAudioDevice final : public AudioDevice
	{
	public:
		AlsaAudioDevice(AudioDir dir, u32 channels, std::string devname, s32 latency)
			: AudioDevice(dir, channels)
			, mDeviceName(std::move(devname))
			, mLatency(latency)
		{
		}

		~AlsaAudioDevice() override { Stop(); }

		bool Start() override
		{
			Stop();
			int err = snd_pcm_open(&mPcm, mDeviceName.c_str(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
			if (err < 0)
			{
				log_cb(RETRO_LOG_ERROR, "USB-Mic: cannot open %s: %s\n", mDeviceName.c_str(), snd_strerror(err));
				mPcm = nullptr;
				return false;
			}
			err = snd_pcm_set_params(mPcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED, mChannels, mSampleRate, 1,
				static_cast<unsigned int>(mLatency) * 1000);
			if (err < 0 || (err = snd_pcm_start(mPcm)) < 0)
			{
				log_cb(RETRO_LOG_ERROR, "USB-Mic: cannot capture from %s: %s\n", mDeviceName.c_str(), snd_strerror(err));
				Stop();
				return false;
			}
			log_cb(RETRO_LOG_INFO, "USB-Mic: capturing from %s at %u Hz\n", mDeviceName.c_str(), mSampleRate);
			return true;
		}

		void Stop() override
		{
			if (mPcm)
				snd_pcm_close(mPcm);
			mPcm = nullptr;
		}

		bool GetFrames(uint32_t* size) override
		{
			*size = 0;
			if (!mPcm)
				return true;
			const snd_pcm_sframes_t avail = snd_pcm_avail(mPcm);
			if (avail < 0)
				Recover(static_cast<int>(avail));
			else
				*size = static_cast<uint32_t>(avail);
			return true;
		}

		uint32_t GetBuffer(int16_t* buff, uint32_t len) override
		{
			if (!mPcm)
				return 0;
			const snd_pcm_sframes_t frames = snd_pcm_readi(mPcm, buff, len);
			if (frames >= 0)
				return static_cast<uint32_t>(frames);
			if (frames != -EAGAIN)
				Recover(static_cast<int>(frames));
			return 0;
		}

		uint32_t SetBuffer(int16_t* buff, uint32_t len) override { return len; }

		void SetResampling(int samplerate) override
		{
			if (static_cast<unsigned int>(samplerate) == mSampleRate)
				return;
			mSampleRate = samplerate;
			if (mPcm)
				Start();
		}

	private:
		// An overrun, from the game not reading for a while, drops what was buffered.
		void Recover(int err)
		{
			if (snd_pcm_recover(mPcm, err, 1) == 0)
				snd_pcm_start(mPcm);
		}

		std::string mDeviceName;
		s32 mLatency;
		unsigned int mSampleRate = 16000;
		snd_pcm_t* mPcm = nullptr;
	};
} // namespace

std::unique_ptr<AudioDevice> AudioDevice::CreateDevice(AudioDir dir, u32 channels, std::string devname, s32 latency)
{
	if (dir != AUDIODIR_SOURCE)
		return nullptr;
	if (devname.empty())
		devname = FindCaptureDevice("/sys", "/proc");
	if (devname.empty())
		return nullptr;
	return std::make_unique<AlsaAudioDevice>(dir, channels, std::move(devname), latency);
}
