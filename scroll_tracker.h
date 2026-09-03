// SPDX-License-Identifier: MIT

#ifndef SCROLL_TRACKER_H
#define SCROLL_TRACKER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Follows map scrolls from the window offset to the buffers. A scroll changes window_x/y at
// input time; the buffers shift on a later render frame, and a shifted buffer makes every
// panned creature look like a real move. So each scroll is queued here as a hint and the
// buffers are hypothesis-tested every redraw to find where it lands. Until then, movement
// detection stays suppressed.
class scroll_trackerst
{
	public:
		using shiftst=std::array<int32_t,2>;

		enum class outcomest : uint8_t
		{
			// Nothing queued, or nothing tested.
			idle,
			// The buffers have not moved yet: the scroll is still in flight.
			waiting,
			// A queued prefix showed up in the buffers; `shift` says which.
			landed,
			// The owed shifts are unknowable now; the caller drops what depended on them.
			abandoned
		};

		struct resultst
		{
			outcomest outcome=outcomest::idle;
			shiftst shift{};
		};

	private:
		// Scrolling faster than detection keeps up: give up rather than test ever more prefixes.
		static constexpr size_t max_pending_shifts=8;
		// Bounds the wait on a scroll that never lands, so suppression cannot stick forever.
		static constexpr int32_t max_pending_age_frames=120;
		// Redraws a prefix may go unmatched before the scroll is written off.
		static constexpr int32_t max_unmatched_frames=4;
		// Redraws detection stays suppressed after scroll activity, so a late buffer settles.
		static constexpr int32_t settle_frames=2;

		// Window scrolls not yet observed in the buffers, oldest first. A signed total would
		// cancel on a reversing drag while both shifts are still owed.
		std::vector<shiftst> pending;
		// Redraws no prefix has matched.
		int32_t unmatched_frames=0;
		// Redraws spent waiting for the buffers to move at all.
		int32_t age=0;
		// Redraws left in which detection stays suppressed.
		int32_t suppress_frames=0;

	public:
		void forget()
			{
			pending.clear();
			unmatched_frames=0;
			age=0;
			}

		void reset()
			{
			forget();
			suppress_frames=0;
			}

		bool has_pending() const
			{
			return !pending.empty();
			}

		// True while detection must stay off: a scroll is owed or the settle window is open.
		bool suppresses() const
			{
			return !pending.empty()||suppress_frames>0;
			}

		// The settle countdown measures redraws, not frames: call once per advanced buffer.
		void spend_settle_frame()
			{
			if(suppress_frames>0)--suppress_frames;
			}

		// A new window offset was seen. Returns true when the queue overflowed and was
		// dropped, in which case the caller must drop its scroll-dependent state too.
		bool queue(int32_t dx,int32_t dy)
			{
			const bool overflowed=pending.size()>=max_pending_shifts;
			if(overflowed)forget();
			pending.push_back({dx,dy});
			unmatched_frames=0;
			suppress_frames=settle_frames;
			return overflowed;
			}

		// Tests the queued scrolls against advanced buffers. `match_ratio(dx,dy)` is the
		// fraction of sprites consistent with the buffers having shifted by (dx,dy), negative
		// when there is nothing to compare.
		template<typename MatchRatio>
		resultst observe(const MatchRatio &match_ratio)
			{
			if(pending.empty())return {};
			// Queued scrolls land in order and may coalesce, so each hypothesis is a prefix.
			// Shortest first: over-retiring leaves owed shifts to be read as movement.
			bool any_data=false;
			shiftst shift{};
			for(size_t count=1;count<=pending.size();++count)
				{
				shift[0]+=pending[count-1][0];
				shift[1]+=pending[count-1][1];
				// A prefix netting to zero is indistinguishable from "nothing landed yet".
				// Accepting it would retire shifts the buffers have still to apply.
				if(shift[0]==0&&shift[1]==0)continue;
				const double ratio=match_ratio(shift[0],shift[1]);
				// Emptiness is per-prefix: a long one can push every sprite out of range
				// while a shorter one still has something to say.
				if(ratio<0.0)continue;
				any_data=true;
				if(ratio>=0.5)
					{
					pending.erase(pending.begin(),pending.begin()+std::ptrdiff_t(count));
					unmatched_frames=0;
					age=0;
					// The scroll is accounted for; the settle window must not block the
					// rebased detection pass.
					suppress_frames=0;
					return {outcomest::landed,shift};
					}
				}
			if(!any_data)
				{
				// Nothing visible to anchor the test on: nothing to animate either.
				forget();
				return {outcomest::abandoned,{}};
				}
			if(match_ratio(0,0)>=0.5&&++age<=max_pending_age_frames)
				{
				unmatched_frames=0;
				return {outcomest::waiting,{}};
				}
			if(++unmatched_frames>max_unmatched_frames)
				{
				// The shift never showed up recognizably: fall back to the safe reset. It may
				// yet land, so do not resume detection on the very next redraw.
				forget();
				suppress_frames=settle_frames;
				return {outcomest::abandoned,{}};
				}
			return {outcomest::idle,{}};
			}
};

#endif
