// SPDX-License-Identifier: MIT

#ifndef TILE_COVERAGE_H
#define TILE_COVERAGE_H

#include "visual_animation.h"

#include <array>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

// The tiles a frame repaints, as viewport tile coordinates.
using tile_coveragest=std::set<std::pair<int32_t,int32_t>>;

// The tiles the proxies of one viewport cover, all of them and per render group, plus for
// each tile the layers that have a proxy targeting it, which the repaint must leave blank.
struct render_coveragest
{
	tile_coveragest all;
	std::array<tile_coveragest,static_cast<size_t>(visual_render_groupst::count)> groups;
	std::unordered_map<int32_t,uint16_t> selected;
};

// The layers with a proxy on a tile, 0 for a tile without one.
inline uint16_t selected_mask(
	const std::unordered_map<int32_t,uint16_t> &selected,
	int32_t index)
{
	const auto found=selected.find(index);
	return found==selected.end()?0:found->second;
}

// Gathers the coverage of a viewport's proxies. `Proxy` provides `layer`, `target_x`,
// `target_y` and `coverage`.
template<typename Proxy>
render_coveragest collect_coverage(
	const std::vector<Proxy> &proxies,
	int32_t dim_y)
{
	render_coveragest coverage;
	for(const Proxy &proxy:proxies)
		{
		coverage.all.insert(proxy.coverage.begin(),proxy.coverage.end());
		coverage.selected[proxy.target_x*dim_y+proxy.target_y]|=
			visual_layer_bit(proxy.layer);
		auto &group=coverage.groups[static_cast<size_t>(visual_render_group(proxy.layer))];
		group.insert(proxy.coverage.begin(),proxy.coverage.end());
		}
	return coverage;
}

// The union of every viewport's coverage. `Render` provides `coverage`.
template<typename Render>
tile_coveragest collect_viewport_coverage(
	const std::vector<Render> &viewports)
{
	tile_coveragest coverage;
	for(const Render &viewport:viewports)
		coverage.insert(
			viewport.coverage.all.begin(),viewport.coverage.all.end());
	return coverage;
}

#endif
