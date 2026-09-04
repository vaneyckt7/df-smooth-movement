// SPDX-License-Identifier: MIT

#ifndef FRAME_STATS_H
#define FRAME_STATS_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

// Per-frame profiling counters behind a runtime switch (`stats on`). Off, each site costs one
// predictable branch; the frame is measured as shipped. The render thread writes and the
// command thread reads, so the counters are relaxed atomics.
//
// `detail` adds timers around every engine repaint, sprite draw and fill batch. Those clock
// reads cost tens of microseconds a frame under some hosts, so they are separate: the totals
// stay honest while the breakdown is off.
class frame_statisticst
{
	using counter=std::atomic<uint64_t>;

	public:
		bool enabled=false;
		bool detail=false;

		counter frames{0};        // hook invocations
		counter rendered{0};      // frames that reached the render pass
		counter sync_us{0};       // buffer comparison and movement detection
		counter collect_us{0};    // render start to the first canvas call
		counter render_us{0};     // whole render pass
		counter engine_us{0};     // inside the engine's tile repaint (detail)
		counter sprite_us{0};     // inside sprite draws (detail)
		counter fill_us{0};       // inside fill batches (detail)
		counter total_us{0};
		counter max_us{0};
		counter tile_repaints{0};
		counter blank_repaints{0};    // repaints requested on tiles with nothing to paint
		counter main_repaints{0};     // repaints on the main level
		counter occluded_repaints{0}; // repaints on lower levels under an opaque main tile
		counter sprites{0};
		counter fills{0};
		counter glides{0};
		counter moving{0};
		counter resting{0};

		static uint64_t now_us()
			{
			return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
			}

		// A clock read only while measuring; 0 otherwise, so callers can time unconditionally.
		uint64_t clock() const {return enabled?now_us():0;}
		uint64_t detail_clock() const {return enabled&&detail?now_us():0;}

		void add(counter &c,uint64_t v=1)
			{
			if(enabled)c.fetch_add(v,std::memory_order_relaxed);
			}

		void note_max(uint64_t v)
			{
			if(!enabled)return;
			uint64_t m=max_us.load(std::memory_order_relaxed);
			while(m<v&&!max_us.compare_exchange_weak(m,v,std::memory_order_relaxed)){}
			}

		void reset()
			{
			for(counter *c:{&frames,&rendered,&sync_us,&collect_us,&render_us,&engine_us,
				&sprite_us,&fill_us,&total_us,&max_us,&tile_repaints,&blank_repaints,
				&main_repaints,&occluded_repaints,&sprites,&fills,&glides,&moving,&resting})
				c->store(0);
			}

		std::string report() const
			{
			const uint64_t f=frames.load(),r=rendered.load();
			const auto per=[](const counter &c,uint64_t n)
				{return n?double(c.load())/double(n):0.0;};
			char line[512];
			std::string out;
			const auto emit=[&](auto... args)
				{std::snprintf(line,sizeof line,args...);out+=line;};
			emit("stats %s, detail %s\n",enabled?"on":"off",detail?"on":"off");
			emit("frames %llu (rendered %llu, glides %llu), avg %.1f us/frame, max %llu us\n",
				(unsigned long long)f,(unsigned long long)r,(unsigned long long)glides.load(),
				per(total_us,f),(unsigned long long)max_us.load());
			emit("  moving frames %llu, resting-sprite frames %llu\n",
				(unsigned long long)moving.load(),(unsigned long long)resting.load());
			emit("  sync %.1f us/frame\n",per(sync_us,f));
			emit("  render %.1f us/rendered frame: collect %.1f, inside engine repaints %.1f, sprite draws %.1f, fills %.1f\n",
				per(render_us,r),per(collect_us,r),per(engine_us,r),per(sprite_us,r),per(fill_us,r));
			emit("  per rendered frame: tile repaints %.1f, sprites %.1f, fills %.1f\n",
				per(tile_repaints,r),per(sprites,r),per(fills,r));
			emit("  repaints: %llu total, %llu blank tiles, %llu on the main level, %llu on lower levels under an opaque main tile\n",
				(unsigned long long)tile_repaints.load(),(unsigned long long)blank_repaints.load(),
				(unsigned long long)main_repaints.load(),(unsigned long long)occluded_repaints.load());
			return out;
			}
};

#endif
