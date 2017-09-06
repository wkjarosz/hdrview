//
// Created by Wojciech Jarosz on 9/3/17.
//

#pragma once

#include "fwd.h"
#include <nanogui/widget.h>

NAMESPACE_BEGIN(nanogui)

class HistogramPanel : public Widget
{
public:
	HistogramPanel(Widget *parent);

	void setImage(const GLImage * img);
	void clear();
	void update();

protected:
	MultiGraph * m_graph = nullptr;
	const GLImage * m_image = nullptr;
	float m_exposure = 1.0f;
	bool m_linear = true;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

NAMESPACE_END(nanogui)