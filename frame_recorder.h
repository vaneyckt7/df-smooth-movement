// SPDX-License-Identifier: MIT

#ifndef FRAME_RECORDER_H
#define FRAME_RECORDER_H

#include "frame_record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Frame recorder for `smooth-movement record`: writes what the render hook reads before
// each frame and what it did after it (see frame_record.h). The recorder owns the file, the
// lock and the copy of every per-tile array it diffs the next frame against; the plugin hands
// it what the hook sees through the callbacks of `capture`, `capture_units` and `finish`,
// which run under the lock and only while a recording is open. Capture runs on the render
// thread; the console thread only starts and stops it.
struct frame_recorderst
{
	// The active viewports of a frame, each with its slot (0..7 lower, 8 main).
	using viewport_listst=std::vector<std::pair<int,const df::graphic_viewportst *>>;

	// What `capture` writes for a frame: the header the hook read and the viewports it sees.
	struct frame_inputst
	{
		frame_record::frame_headerst header;
		viewport_listst viewports;
	};

	std::mutex mutex;
	FILE *file=nullptr;
	std::string path;
	uint32_t remaining=0;
	uint32_t written=0;
	bool failed=false;
	bool reset_pending=false;   // first captured frame starts from fresh visual state
	bool units_pending=false;   // capture wrote the viewports; the unit list is still owed
	frame_record::writerst writer;
	// Raw bytes of every per-tile array as of the last captured frame, per slot and array.
	std::vector<uint8_t> previous[frame_record::slot_count*frame_record::buffer_count];

	bool start(const std::string &to,uint32_t frames)
		{
		const std::lock_guard<std::mutex> lock(mutex);
		close();
		file=std::fopen(to.c_str(),"wb");
		if(file==nullptr)return false;
		path=to;
		remaining=frames;
		written=0;
		failed=false;
		reset_pending=true;
		units_pending=false;
		for(auto &p:previous)p.clear();
		writer.bytes.clear();
		frame_record::write_file_header(writer);
		flush();
		return !failed;
		}

	void stop()
		{
		const std::lock_guard<std::mutex> lock(mutex);
		close();
		}

	bool running()
		{
		const std::lock_guard<std::mutex> lock(mutex);
		return file!=nullptr;
		}

	// Called by the render hook at its start. `begin(first)` returns the frame's input; `first`
	// is true on the first frame of a recording, where the plugin resets its animation and
	// camera state so the replay, which starts from plugin_enable, sees the same start.
	template<typename Begin>
	void capture(const Begin &begin)
		{
		const std::lock_guard<std::mutex> lock(mutex);
		if(file==nullptr)return;
		const bool first=reset_pending;
		reset_pending=false;
		const frame_inputst input=begin(first);
		writer.bytes.clear();
		frame_record::write_frame_header(writer,input.header);
		writer.u8(uint8_t(input.viewports.size()));
		for(const auto &entry:input.viewports)
			{
			const int slot=entry.first;
			const df::graphic_viewportst *vp=entry.second;
			frame_record::write_viewport_header(writer,{slot,vp->dim_x,vp->dim_y,
				vp->clipx[0],vp->clipx[1],vp->clipy[0],vp->clipy[1],vp->screen_x,vp->screen_y});
			const size_t count=size_t(vp->dim_x)*size_t(vp->dim_y);
			int index=0;
			frame_record::for_each_buffer(*vp,[&](auto pointer)
				{
				using T=std::remove_cv_t<std::remove_pointer_t<decltype(pointer)>>;
				std::vector<uint8_t> &last=previous[slot*frame_record::buffer_count+index++];
				if(pointer==nullptr)
					{
					writer.u8(0);
					last.clear();
					return;
					}
				writer.u8(1);
				const T *previous_words=last.size()==count*sizeof(T)?
					reinterpret_cast<const T *>(last.data()):nullptr;
				writer.words(pointer,previous_words,count);
				last.resize(count*sizeof(T));
				std::memcpy(last.data(),pointer,count*sizeof(T));
				});
			}
		units_pending=true;
		}

	// Called where the hook looks units up (after the camera update, which can move the
	// window). `collect()` returns the unit records; it runs only while the list is owed.
	template<typename Collect>
	void capture_units(const Collect &collect)
		{
		const std::lock_guard<std::mutex> lock(mutex);
		if(file==nullptr||!units_pending)return;
		units_pending=false;
		const std::vector<frame_record::unit_recordst> records=collect();
		frame_record::write_units(writer,records);
		}

	// Called at the hook's end. `viewports()` lists the active viewports again, for the count
	// of entries the game changed while the hook ran.
	template<typename Viewports>
	void finish(uint32_t repaints,bool painted,const Viewports &viewports)
		{
		const std::lock_guard<std::mutex> lock(mutex);
		if(file==nullptr||writer.bytes.empty())return;
		if(units_pending){frame_record::write_units(writer,{});units_pending=false;}
		frame_record::write_frame_result(writer,
			{repaints,painted,changed_since_capture(viewports())});
		flush();
		if(failed)return;
		++written;
		if(--remaining==0)close();
		}

	template<typename Out>
	void status(Out &out)
		{
		const std::lock_guard<std::mutex> lock(mutex);
		if(file!=nullptr)
			out.print("recording: {} of {} frames to {}\n",written,written+remaining,path);
		else if(failed)
			out.print("recording: failed writing {} after {} frames\n",path,written);
		else if(written!=0)
			out.print("recording: {} frames written to {}\n",written,path);
		else
			out.print("recording: off\n");
		}

private:
	// Entries of the given viewports' per-tile arrays that differ from the captured copy: the
	// game changed them while the hook ran, so the replay saw different input than the hook did.
	uint32_t changed_since_capture(const viewport_listst &viewports) const
		{
		uint32_t changed=0;
		for(const auto &entry:viewports)
			{
			const int slot=entry.first;
			const df::graphic_viewportst *vp=entry.second;
			const size_t count=size_t(vp->dim_x)*size_t(vp->dim_y);
			int index=0;
			frame_record::for_each_buffer(*vp,[&](auto pointer)
				{
				using T=std::remove_cv_t<std::remove_pointer_t<decltype(pointer)>>;
				const std::vector<uint8_t> &last=previous[slot*frame_record::buffer_count+index++];
				if(pointer==nullptr||last.size()!=count*sizeof(T))return;
				const T *words=reinterpret_cast<const T *>(last.data());
				for(size_t i=0;i<count;++i)
					if(std::memcmp(&pointer[i],&words[i],sizeof(T))!=0)++changed;
				});
			}
		return changed;
		}

	// Writes the pending bytes and pushes them to the file, so a crash of the game loses at
	// most the frame in progress.
	void flush()
		{
		if(!writer.bytes.empty()&&
			(std::fwrite(writer.bytes.data(),1,writer.bytes.size(),file)!=writer.bytes.size()||
			std::fflush(file)!=0))
			{
			failed=true;
			close();
			}
		writer.bytes.clear();
		}

	void close()
		{
		if(file!=nullptr&&std::fclose(file)!=0)failed=true;
		file=nullptr;
		remaining=0;
		writer.bytes.clear();
		}
};

#endif
