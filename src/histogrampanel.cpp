//
// Created by Wojciech Jarosz on 9/3/17.
//

#include "histogrampanel.h"
#include "multigraph.h"
#include "hdrimage.h"
#include <nanogui/nanogui.h>
#include "common.h"

using namespace std;
using namespace Eigen;

namespace
{
MatrixX3f makeHistograms(const HDRImage & img, int numBins, float exposure = 1.f, bool linear = true);
}

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

void HistogramPanel::setImage(const HDRImage & img)
{
	m_image = &img;
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

	MatrixX3f hist = makeHistograms(*m_image, 256, m_exposure, m_linear);
	m_graph->setValues(hist.col(0)/hist.block(1,0,254,1).maxCoeff(), 0);
	m_graph->setValues(hist.col(1)/hist.block(1,1,254,1).maxCoeff(), 1);
	m_graph->setValues(hist.col(2)/hist.block(1,2,254,1).maxCoeff(), 2);
}

NAMESPACE_END(nanogui)

namespace
{

MatrixX3f makeHistograms(const HDRImage & img, int numBins, float exposure, bool linear)
{
	MatrixX3f hist = MatrixX3f::Zero(numBins, 3);
	float d = 1.f / (img.width() * img.height());
	for (int y = 0; y < img.height(); ++y)
		for (int x = 0; x < img.width(); ++x)
		{
			Color4 c = (linear ? img(x,y) : toSRGB(img(x,y))) * exposure;
			hist(clamp(int(floor(c[0] * numBins)), 0, numBins - 1), 0) += d;
			hist(clamp(int(floor(c[1] * numBins)), 0, numBins - 1), 1) += d;
			hist(clamp(int(floor(c[2] * numBins)), 0, numBins - 1), 2) += d;
		}
	return hist;
}

}