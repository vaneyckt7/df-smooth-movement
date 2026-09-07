// SPDX-License-Identifier: MIT

#ifndef FRAME_STATS_H
#define FRAME_STATS_H

#include <atomic>
#include <chrono>
#include <cstdint>

// Frame timing for `smooth-movement stats`. Counting is always on: one increment per frame,
// one per painted frame, one per repaint by the game. The clock is read three times per frame
// while enabled. Sync covers movement detection across every viewport; render covers
// everything after it, including the camera update and carried-item lookup that feed the draw
// decision, then proxy collection, tile blanking, repaints by the game and sprites. The render
// thread writes, the console thread reads and clears, so the fields are relaxed atomics; a
// clear that lands mid-frame skews that one frame and nothing else.
struct frame_statsst
{
	std::atomic<bool> enabled{false};
	std::atomic<uint64_t> frames{0};
	std::atomic<uint64_t> painted{0};
	std::atomic<uint64_t> repaints{0};
	std::atomic<uint64_t> timed{0};
	std::atomic<uint64_t> sync_us{0};
	std::atomic<uint64_t> sync_max_us{0};
	std::atomic<uint64_t> render_us{0};
	std::atomic<uint64_t> render_max_us{0};

	static uint64_t now_us()
		{
		return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		}

	void clear()
		{
		for(std::atomic<uint64_t> *counter:{&frames,&painted,&repaints,&timed,
			&sync_us,&sync_max_us,&render_us,&render_max_us})
			counter->store(0,std::memory_order_relaxed);
		}

	static void add(std::atomic<uint64_t> &total,std::atomic<uint64_t> &max,uint64_t us)
		{
		total.fetch_add(us,std::memory_order_relaxed);
		uint64_t seen=max.load(std::memory_order_relaxed);
		while(seen<us&&!max.compare_exchange_weak(seen,us,std::memory_order_relaxed)){}
		}

	void add_sync(uint64_t us){add(sync_us,sync_max_us,us);}
	void add_render(uint64_t us){add(render_us,render_max_us,us);}

	// Prints the counters to a DFHack console (`color_ostream`), or anything else with the
	// same `print` member; a template so this header needs no DFHack include.
	template<typename Out>
	void print(Out &out) const
		{
		const auto get=[](const std::atomic<uint64_t> &v)
			{return v.load(std::memory_order_relaxed);};
		const uint64_t frame_count=get(frames),painted_count=get(painted),
			repaint_count=get(repaints),timed_count=get(timed),
			sync_total_us=get(sync_us),render_total_us=get(render_us);
		out.print("frame stats: {}\n",enabled?"on":"off");
		if(frame_count==0)
			{
			out.print("no frames counted\n");
			return;
			}
		const auto mean=[](uint64_t total,uint64_t count)
			{
			return count==0?0.0:double(total)/double(count);
			};
		out.print("frames: {} ({} painted, {:.0f}%)\n",
			frame_count,painted_count,100.0*mean(painted_count,frame_count));
		out.print("game tile repaints: {} ({:.1f} per frame, {:.1f} per painted frame)\n",
			repaint_count,mean(repaint_count,frame_count),mean(repaint_count,painted_count));
		if(timed_count==0)return;
		out.print("timed frames: {}\n",timed_count);
		out.print("sync: mean {:.0f} us, max {} us\n",
			mean(sync_total_us,timed_count),get(sync_max_us));
		out.print("render: mean {:.0f} us, max {} us\n",
			mean(render_total_us,timed_count),get(render_max_us));
		out.print("total: mean {:.0f} us per timed frame\n",
			mean(sync_total_us+render_total_us,timed_count));
		}
};

#endif
