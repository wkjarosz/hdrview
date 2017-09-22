//
// Created by Wojciech Jarosz on 9/3/17.
//

#include "editimagepanel.h"
#include "common.h"
#include "glimage.h"
#include "hdrviewer.h"
#include "hdrimage.h"
#include "hdrimagemanager.h"
#include "envmap.h"
#include "colorspace.h"

using namespace std;

namespace
{

Button * createColorSpaceButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static string name = "Convert color space...";
	static EColorSpace src = LinearSRGB_CS, dst = CIEXYZ_CS;
	auto b = new Button(parent, name, ENTYPO_ICON_PALETTE);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
	               {
		               FormHelper *gui = new FormHelper(screen);
		               gui->setFixedSize(Vector2i(125, 20));

		               auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);

		               gui->addVariable("Source:", src, true)
		                  ->setItems(colorSpaceNames());
		               gui->addVariable("Destination:", dst, true)
		                  ->setItems(colorSpaceNames());

		               gui->addButton("Cancel", [&, window]() { window->dispose(); })
		                  ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
		               gui->addButton("OK", [&, window]()
		               {
			               imageMgr->modifyImage([&](HDRImage &img)
			                                     {
				                                     auto undo = new FullImageUndo(img);
				                                     img = img.unaryExpr([](const Color4 & c){return c.convert(dst, src);}).eval();
				                                     return undo;
			                                     });
			               window->dispose();
		               })->setIcon(ENTYPO_ICON_CHECK);

		               window->center();
		               window->requestFocus();
	               });
	return b;
}

Button * createExposureGammaButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static string name = "Exposure/Gamma...";
	static float exposure = 0.0f;
	static float gamma = 1.0f;
	auto b = new Button(parent, name, ENTYPO_ICON_ADJUST);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
       {
           FormHelper *gui = new FormHelper(screen);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Exposure:", exposure);
           w->setSpinnable(true);
           w->setMinValue(-10.f);
		   w->setMaxValue( 10.f);
           w = gui->addVariable("Gamma:", gamma);
           w->setSpinnable(true);
           w->setMinValue(0.0001f);
		   w->setMaxValue(10.0f);

           gui->addButton("Cancel", [&, window]() { window->dispose(); })
              ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&, window]()
           {
               imageMgr->modifyImage([&](HDRImage &img)
                                     {
	                                     auto undo = new FullImageUndo(img);
	                                     img = (Color4(pow(2.0f, exposure)) * img).pow(Color4(1.0f/gamma));
	                                     return undo;
                                     });
               window->dispose();
           })->setIcon(ENTYPO_ICON_CHECK);

           window->center();
           window->requestFocus();
       });
	return b;
}

Button * createGaussianFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static bool exact = false;
	static string name = "Gaussian blur...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
       {
           FormHelper *gui = new FormHelper(screen);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
           // window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Width:", width);
           w->setSpinnable(true);
           w->setMinValue(0.0f);
           w = gui->addVariable("Height:", height);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

           gui->addVariable("Border mode X:", borderModeX, true)
              ->setItems(HDRImage::borderModeNames());
           gui->addVariable("Border mode Y:", borderModeY, true)
              ->setItems(HDRImage::borderModeNames());

	       gui->addVariable("Exact (slow!):", exact, true);

           gui->addButton("Cancel", [&, window]() { window->dispose(); })
              ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&, window]()
           {
	           imageMgr->modifyImage([&](HDRImage &img)
	                               {
		                               auto undo = new FullImageUndo(img);
		                               img = exact ? img.GaussianBlurred(width, height, borderModeX, borderModeY) :
		                                     img.fastGaussianBlurred(width, height, borderModeX, borderModeY);
		                               return undo;
	                               });
               window->dispose();
           })->setIcon(ENTYPO_ICON_CHECK);

           window->center();
           window->requestFocus();
       });
	return b;
}

Button * createBoxFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float width = 1.0f, height = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Box blur...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
       {
           FormHelper *gui = new FormHelper(screen);
           gui->setFixedSize(Vector2i(75, 20));

           auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

           auto w = gui->addVariable("Width:", width);
           w->setSpinnable(true);
           w->setMinValue(0.0f);
           w = gui->addVariable("Height:", height);
           w->setSpinnable(true);
           w->setMinValue(0.0f);

           gui->addVariable("Border mode X:", borderModeX, true)
              ->setItems(HDRImage::borderModeNames());
           gui->addVariable("Border mode Y:", borderModeY, true)
              ->setItems(HDRImage::borderModeNames());

           gui->addButton("Cancel", [&, window]() { window->dispose(); })
              ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
           gui->addButton("OK", [&, window]()
           {
	           imageMgr->modifyImage([&](HDRImage &img)
	                               {
		                               auto undo = new FullImageUndo(img);
		                               img = img.boxBlurred(width, height, borderModeX, borderModeY);
		                               return undo;
	                               });
	           window->dispose();
           })->setIcon(ENTYPO_ICON_CHECK);

           window->center();
           window->requestFocus();
       });
	return b;
}

Button * createBilateralFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float rangeSigma = 1.0f, valueSigma = 0.1f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Bilateral filter...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
	               {
		               FormHelper *gui = new FormHelper(screen);
		               gui->setFixedSize(Vector2i(75, 20));

		               auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

		               auto w = gui->addVariable("Range sigma:", rangeSigma);
		               w->setSpinnable(true);
		               w->setMinValue(0.0f);
		               w = gui->addVariable("Value sigma:", valueSigma);
		               w->setSpinnable(true);
		               w->setMinValue(0.0f);

		               gui->addVariable("Border mode X:", borderModeX, true)
		                  ->setItems(HDRImage::borderModeNames());
		               gui->addVariable("Border mode Y:", borderModeY, true)
		                  ->setItems(HDRImage::borderModeNames());

		               gui->addButton("Cancel", [&, window]() { window->dispose(); })
		                  ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
		               gui->addButton("OK", [&, window]()
		               {
			               imageMgr->modifyImage([&](HDRImage &img)
			                                   {
				                                   auto undo = new FullImageUndo(img);
				                                   img = img.bilateralFiltered(valueSigma, rangeSigma, borderModeX,
				                                                               borderModeY);
				                                   return undo;
			                                   });
			               window->dispose();
		               })->setIcon(ENTYPO_ICON_CHECK);

		               //imageMgr->performLayout();
		               window->center();
		               window->requestFocus();
	               });
	return b;
}

Button * createUnsharpMaskFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float sigma = 1.0f, strength = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Unsharp mask...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
	               {
		               FormHelper *gui = new FormHelper(screen);
		               gui->setFixedSize(Vector2i(75, 20));

		               auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

		               auto w = gui->addVariable("Sigma:", sigma);
		               w->setSpinnable(true);
		               w->setMinValue(0.0f);
		               w = gui->addVariable("Strength:", strength);
		               w->setSpinnable(true);
		               w->setMinValue(0.0f);

		               gui->addVariable("Border mode X:", borderModeX, true)
		                  ->setItems(HDRImage::borderModeNames());
		               gui->addVariable("Border mode Y:", borderModeY, true)
		                  ->setItems(HDRImage::borderModeNames());

		               gui->addButton("Cancel", [&, window]() { window->dispose(); })
		                  ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
		               gui->addButton("OK", [&, window]()
		               {
			               imageMgr->modifyImage([&](HDRImage &img)
			                                   {
				                                   auto undo = new FullImageUndo(img);
				                                   img =
					                                   img.unsharpMasked(sigma, strength, borderModeX, borderModeY);
				                                   return undo;
			                                   });
			               window->dispose();
		               })->setIcon(ENTYPO_ICON_CHECK);

		               //imageMgr->performLayout();
		               window->center();
		               window->requestFocus();
	               });
	return b;
}

Button * createMedianFilterButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static float radius = 1.0f;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static string name = "Median filter...";
	auto b = new Button(parent, name, ENTYPO_ICON_DROPLET);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
	               {
		               FormHelper *gui = new FormHelper(screen);
		               gui->setFixedSize(Vector2i(75, 20));

		               auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

		               auto w = gui->addVariable("Radius:", radius);
		               w->setSpinnable(true);
		               w->setMinValue(0.0f);

		               gui->addVariable("Border mode X:", borderModeX, true)
		                  ->setItems(HDRImage::borderModeNames());
		               gui->addVariable("Border mode Y:", borderModeY, true)
		                  ->setItems(HDRImage::borderModeNames());

		               gui->addButton("Cancel", [&, window]() { window->dispose(); })
		                  ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
		               gui->addButton("OK", [&, window]()
		               {
			               imageMgr->modifyImage([&](HDRImage &img)
			                                   {
				                                   auto undo = new FullImageUndo(img);
				                                   img = img.medianFiltered(radius, borderModeX, borderModeY);
				                                   return undo;
			                                   });
			               window->dispose();
		               })->setIcon(ENTYPO_ICON_CHECK);

		               //imageMgr->performLayout();
		               window->center();
		               window->requestFocus();
	               });
	return b;
}

Button * createResizeButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static int width = 128, height = 128;
	static string name = "Resize...";
	auto b = new Button(parent, name, ENTYPO_ICON_RESIZE_FULL);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
	               {
		               FormHelper *gui = new FormHelper(screen);
		               gui->setFixedSize(Vector2i(75, 20));

		               auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
		               window->setModal(true);

		               width = imageMgr->currentImage()->width();
		               auto w = gui->addVariable("Width:", width);
		               w->setSpinnable(true);
		               w->setMinValue(1);

		               height = imageMgr->currentImage()->height();
		               w = gui->addVariable("Height:", height);
		               w->setSpinnable(true);
		               w->setMinValue(1);

		               gui->addButton("Cancel", [&, window]() { window->dispose(); })
		                  ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
		               gui->addButton("OK", [&, window]()
		               {
			               imageMgr->modifyImage([&](HDRImage &img)
			                                   {
				                                   auto undo = new FullImageUndo(img);
				                                   img = img.resized(width, height);
				                                   return undo;
			                                   });
			               window->dispose();
		               })->setIcon(ENTYPO_ICON_CHECK);

		               //imageMgr->performLayout();
		               window->center();
		               window->requestFocus();
	               });
	return b;
}

Button * createResampleButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	enum MappingMode
	{
		ANGULAR_MAP = 0,
		MIRROR_BALL,
		LAT_LONG,
		CUBE_MAP
	};
	static MappingMode from = ANGULAR_MAP, to = ANGULAR_MAP;
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static int width = 128, height = 128;
	static HDRImage::BorderMode borderModeX = HDRImage::EDGE, borderModeY = HDRImage::EDGE;
	static int samples = 1;

	static string name = "Remap...";
	auto b = new Button(parent, name, ENTYPO_ICON_MAP);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
	               {
		               FormHelper *gui = new FormHelper(screen);
		               gui->setFixedSize(Vector2i(125, 20));

		               auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

		               width = imageMgr->currentImage()->width();
		               auto w = gui->addVariable("Width:", width);
		               w->setSpinnable(true);
		               w->setMinValue(1);

		               height = imageMgr->currentImage()->height();
		               w = gui->addVariable("Height:", height);
		               w->setSpinnable(true);
		               w->setMinValue(1);

		               gui->addVariable("Source parametrization:", from, true)
		                  ->setItems({"Angular map", "Mirror ball", "Longitude-latitude", "Cube map"});
		               gui->addVariable("Target parametrization:", to, true)
		                  ->setItems({"Angular map", "Mirror ball", "Longitude-latitude", "Cube map"});
		               gui->addVariable("Sampler:", sampler, true)
		                  ->setItems(HDRImage::samplerNames());
		               gui->addVariable("Border mode X:", borderModeX, true)
		                  ->setItems(HDRImage::borderModeNames());
		               gui->addVariable("Border mode Y:", borderModeY, true)
		                  ->setItems(HDRImage::borderModeNames());

		               w = gui->addVariable("Super-samples:", samples);
		               w->setSpinnable(true);
		               w->setMinValue(1);

		               gui->addButton("Cancel", [&, window]() { window->dispose(); })
		                  ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
		               gui->addButton("OK", [&, window]()
		               {
			               // by default use a no-op passthrough warp function
			               function<Vector2f(const Vector2f &)> warp = [](const Vector2f &uv) { return uv; };

			               UV2XYZFn dst2xyz;
			               XYZ2UVFn xyz2src;

			               if (from != to)
			               {
				               if (from == ANGULAR_MAP)
					               xyz2src = XYZToAngularMap;
				               else if (from == MIRROR_BALL)
					               xyz2src = XYZToMirrorBall;
				               else if (from == LAT_LONG)
					               xyz2src = XYZToLatLong;
				               else if (from == CUBE_MAP)
					               xyz2src = XYZToCubeMap;

				               if (to == ANGULAR_MAP)
					               dst2xyz = angularMapToXYZ;
				               else if (to == MIRROR_BALL)
					               dst2xyz = mirrorBallToXYZ;
				               else if (to == LAT_LONG)
					               dst2xyz = latLongToXYZ;
				               else if (to == CUBE_MAP)
					               dst2xyz = cubeMapToXYZ;

				               warp = [&](const Vector2f &uv) { return xyz2src(dst2xyz(Vector2f(uv(0), uv(1)))); };
			               }

			               imageMgr->modifyImage([&](HDRImage &img)
			                                   {
				                                   auto undo = new FullImageUndo(img);
				                                   img = img.resampled(width, height, warp, samples, sampler,
				                                                       borderModeX, borderModeY);
				                                   return undo;
			                                   });
			               window->dispose();
		               })->setIcon(ENTYPO_ICON_CHECK);

		               //imageMgr->performLayout();
		               window->center();
		               window->requestFocus();
	               });
	return b;
}

Button * createShiftButton(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
{
	static HDRImage::Sampler sampler = HDRImage::BILINEAR;
	static HDRImage::BorderMode borderModeX = HDRImage::REPEAT, borderModeY = HDRImage::REPEAT;
	static float dx = 0.f, dy = 0.f;
	static string name = "Shift...";
	auto b = new Button(parent, name, ENTYPO_ICON_SWITCH);
	b->setFixedHeight(21);
	b->setCallback([&, parent, screen, imageMgr]()
	               {
		               FormHelper *gui = new FormHelper(screen);
		               gui->setFixedSize(Vector2i(125, 20));

		               auto window = gui->addWindow(Eigen::Vector2i(10, 10), name);
//           window->setModal(true);    // BUG: this should be set to modal, but doesn't work with comboboxes

		               auto w = gui->addVariable("X offset:", dx);
		               w->setSpinnable(true);

		               w = gui->addVariable("Y offset:", dy);
		               w->setSpinnable(true);

		               gui->addVariable("Sampler:", sampler, true)
		                  ->setItems(HDRImage::samplerNames());
		               gui->addVariable("Border mode X:", borderModeX, true)
		                  ->setItems(HDRImage::borderModeNames());
		               gui->addVariable("Border mode Y:", borderModeY, true)
		                  ->setItems(HDRImage::borderModeNames());

		               gui->addButton("Cancel", [&, window]() { window->dispose(); })
		                  ->setIcon(ENTYPO_ICON_CIRCLED_CROSS);
		               gui->addButton("OK", [&, window]()
		               {
			               imageMgr->modifyImage([&](HDRImage &img)
			                                   {
				                                   // by default use a no-op passthrough warp function
				                                   function<Vector2f(const Vector2f &)> shift = [&](const Vector2f &uv)
				                                   {
					                                   return (uv + Vector2f(dx / img.width(), dy / img.height()))
						                                   .eval();
				                                   };

				                                   auto undo = new FullImageUndo(img);
				                                   img = img.resampled(img.width(), img.height(), shift, 1, sampler,
				                                                       borderModeX, borderModeY);
				                                   return undo;
			                                   });
			               window->dispose();
		               })->setIcon(ENTYPO_ICON_CHECK);

		               //imageMgr->performLayout();
		               window->center();
		               window->requestFocus();
	               });
	return b;
}

}


EditImagePanel::EditImagePanel(Widget *parent, HDRViewScreen * screen, HDRImageManager * imageMgr)
	: Widget(parent), m_screen(screen), m_imageMgr(imageMgr)
{
	setLayout(new GroupLayout(2, 4, 8, 10));

	new Label(this, "History", "sans-bold");

	auto buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 2));

	m_undoButton = new Button(buttonRow, "Undo", ENTYPO_ICON_REPLY);
	m_undoButton->setCallback([&](){m_imageMgr->undo();});
	m_redoButton = new Button(buttonRow, "Redo", ENTYPO_ICON_FORWARD);
	m_redoButton->setCallback([&](){m_imageMgr->redo();});

	new Label(this, "Transformations", "sans-bold");

	auto grid = new Widget(this);
	grid->setLayout(new GridLayout(Orientation::Vertical, 2, Alignment::Fill, 0, 2));

	// flip h
	m_filterButtons.push_back(new Button(grid, "Flip H", ENTYPO_ICON_LEFT_BOLD));
	m_filterButtons.back()->setCallback([&](){m_screen->flipImage(true);});
	m_filterButtons.back()->setFixedHeight(21);

	// flip v
	m_filterButtons.push_back(new Button(grid, "Flip V", ENTYPO_ICON_DOWN_BOLD));
	m_filterButtons.back()->setCallback([&](){m_screen->flipImage(false);});
	m_filterButtons.back()->setFixedHeight(21);

	// rotate cw
	m_filterButtons.push_back(new Button(grid, "Rotate CW", ENTYPO_ICON_CW));
	m_filterButtons.back()->setFixedHeight(21);
	m_filterButtons.back()->setCallback([&]()
       {
	       m_imageMgr->modifyImage([&](HDRImage &img)
	                             {
		                             img = img.rotated90CW();
		                             return new LambdaUndo([](HDRImage &img2) { img2 = img2.rotated90CCW(); },
		                                                   [](HDRImage &img2) { img2 = img2.rotated90CW(); });
	                             });
       });

	// rotate ccw
	m_filterButtons.push_back(new Button(grid, "Rotate CCW", ENTYPO_ICON_CCW));
	m_filterButtons.back()->setFixedHeight(21);
	m_filterButtons.back()->setCallback([&]()
       {
	       m_imageMgr->modifyImage([&](HDRImage &img)
	                             {
		                             img = img.rotated90CCW();
		                             return new LambdaUndo([](HDRImage &img2) { img2 = img2.rotated90CW(); },
		                                                   [](HDRImage &img2) { img2 = img2.rotated90CCW(); });
	                             });
       });

	buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, 2));

	// shift
	m_filterButtons.push_back(createShiftButton(buttonRow, m_screen, m_imageMgr));

	new Label(this, "Adjustments", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, 2));
	m_filterButtons.push_back(createExposureGammaButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createColorSpaceButton(buttonRow, m_screen, m_imageMgr));

	new Label(this, "Resize/resample", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 2, Alignment::Fill, 0, 2));
	m_filterButtons.push_back(createResizeButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createResampleButton(buttonRow, m_screen, m_imageMgr));

	new Label(this, "Filters", "sans-bold");
	buttonRow = new Widget(this);
	buttonRow->setLayout(new GridLayout(Orientation::Horizontal, 1, Alignment::Fill, 0, 2));
	m_filterButtons.push_back(createGaussianFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createBoxFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createBilateralFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createUnsharpMaskFilterButton(buttonRow, m_screen, m_imageMgr));
	m_filterButtons.push_back(createMedianFilterButton(buttonRow, m_screen, m_imageMgr));
}


void EditImagePanel::enableDisableButtons()
{
	if (m_undoButton)
		m_undoButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImage()->hasUndo());
	if (m_redoButton)
		m_redoButton->setEnabled(m_imageMgr->currentImage() && m_imageMgr->currentImage()->hasRedo());

	for_each(m_filterButtons.begin(), m_filterButtons.end(),
	         [&](Widget * w){w->setEnabled(m_imageMgr->currentImage()); });
}