//
// Created by Wojciech Jarosz on 9/3/17.
//

#include "histogrampanel.h"
#include "multigraph.h"
#include "glimage.h"
#include <nanogui/nanogui.h>
#include "common.h"

using namespace std;
using namespace Eigen;

NAMESPACE_BEGIN(nanogui)

/*!
 * @param parent	The parent widget
 * @param caption 	Caption text
 * @param fg 		The foreground color of the first plot
 * @param v 		The value vector for the first plot
 */
HistogramPanel::HistogramPanel(Widget *parent)
	: Widget(parent)
{
	setLayout(new BoxLayout(Orientation::Vertical,
	                        Alignment::Fill, 0, 10));
	m_graph = new MultiGraph(this, "", Color(255, 0, 0, 255));
	m_graph->addPlot(Color(0, 255, 0, 128));
	m_graph->addPlot(Color(0, 0, 255, 85));

	auto w = new Widget(this);
	w->setLayout(new BoxLayout(Orientation::Horizontal,
	              Alignment::Middle, 0, 2));

	new Label(w, "EV", "sans");
	auto exposureSlider = new Slider(w);
	auto exposureTextBox = new FloatBox<float>(w, 0.0f);
	auto linearToggle = new Button(w, "", ENTYPO_ICON_VOLUME);

	exposureTextBox->numberFormat("%1.2f");
	exposureTextBox->setEditable(true);
	exposureTextBox->setFixedWidth(35);
	exposureTextBox->setAlignment(TextBox::Alignment::Right);
	exposureTextBox->setCallback([&,exposureSlider](float ev)
		{
			exposureSlider->setValue(ev);
			m_exposure = pow(2.f, ev);
			update();
		});
	exposureSlider->setCallback([&,exposureTextBox](float v)
         {
             float ev = round(4*v) / 4.0f;
             exposureTextBox->setValue(ev);
             m_exposure = pow(2.f, ev);
         });
	exposureSlider->setFinalCallback([&,exposureTextBox](float v)
		{
			update();
		});
	exposureSlider->setFixedWidth(95);
	exposureSlider->setRange({-9.0f,9.0f});
	exposureTextBox->setValue(0.0f);

	linearToggle->setFlags(Button::ToggleButton);
	linearToggle->setFixedSize(Vector2i(19, 19));
	linearToggle->setTooltip("Toggle between linear and sRGB histogram computation.");
	linearToggle->setPushed(true);
	linearToggle->setChangeCallback([&](bool b)
	{
		m_linear = b;
		update();
	});
}

void HistogramPanel::setImage(const GLImage * img)
{
	m_image = img;
	update();
}

void HistogramPanel::clear()
{
	m_graph->setValues(VectorXf(), 0);
	m_graph->setValues(VectorXf(), 1);
	m_graph->setValues(VectorXf(), 2);
}

void HistogramPanel::update()
{
	if (!m_image)
	{
		clear();
		return;
	}

	auto hist = m_image->histogram(m_linear, m_exposure);
	int numBins = hist.rows();
	float maxValue = hist.block(1,0,numBins-2,3).maxCoeff();
	m_graph->setValues(hist.col(0)/maxValue, 0);
	m_graph->setValues(hist.col(1)/maxValue, 1);
	m_graph->setValues(hist.col(2)/maxValue, 2);
}

NAMESPACE_END(nanogui)