#pragma once

#include <nanogui/widget.h>

class Well : public nanogui::Widget
{
public:
	Well(nanogui::Widget *parent, float radius = 3.0f,
	     const nanogui::Color & inner = nanogui::Color(0, 32),
	     const nanogui::Color & outer = nanogui::Color(0, 92));

	/// Return the inner well color
	const nanogui::Color & innerColor() const { return m_innerColor; }
	/// Set the inner well color
	void setInnerColor(const nanogui::Color & innerColor) { m_innerColor = innerColor; }

	/// Return the outer well color
	const nanogui::Color & outerColor() const { return m_outerColor; }
	/// Set the outer well color
	void setOuterColor(const nanogui::Color & outerColor) { m_outerColor = outerColor; }

    virtual void draw(NVGcontext* ctx) override;

protected:
	float m_radius;
	nanogui::Color m_innerColor, m_outerColor;

public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};
