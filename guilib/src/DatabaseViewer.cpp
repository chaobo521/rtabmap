/*
Copyright (c) 2010-2016, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "rtabmap/gui/DatabaseViewer.h"
#include "rtabmap/gui/CloudViewer.h"
#include "ui_DatabaseViewer.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QDesktopWidget>
#include <QColorDialog>
#include <QGraphicsLineItem>
#include <QtGui/QCloseEvent>
#include <QGraphicsOpacityEffect>
#include <QtCore/QBuffer>
#include <QtCore/QTextStream>
#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QThread>
#include <rtabmap/utilite/ULogger.h>
#include <rtabmap/utilite/UDirectory.h>
#include <rtabmap/utilite/UConversion.h>
#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <rtabmap/utilite/UTimer.h>
#include <rtabmap/utilite/UFile.h>
#include "rtabmap/utilite/UPlot.h"
#include "rtabmap/core/DBDriver.h"
#include "rtabmap/gui/KeypointItem.h"
#include "rtabmap/gui/CloudViewer.h"
#include "rtabmap/utilite/UCv2Qt.h"
#include "rtabmap/core/util3d.h"
#include "rtabmap/core/util3d_transforms.h"
#include "rtabmap/core/util3d_filtering.h"
#include "rtabmap/core/util3d_surface.h"
#include "rtabmap/core/util3d_registration.h"
#include "rtabmap/core/util3d_mapping.h"
#include "rtabmap/core/util2d.h"
#include "rtabmap/core/Signature.h"
#include "rtabmap/core/Memory.h"
#include "rtabmap/core/Features2d.h"
#include "rtabmap/core/Compression.h"
#include "rtabmap/core/Graph.h"
#include "rtabmap/core/Stereo.h"
#include "rtabmap/core/Optimizer.h"
#include "rtabmap/core/RegistrationVis.h"
#include "rtabmap/core/RegistrationIcp.h"
#include "rtabmap/core/OccupancyGrid.h"
#include "rtabmap/core/GeodeticCoords.h"
#include "rtabmap/core/Recovery.h"
#include "rtabmap/gui/DataRecorder.h"
#include "rtabmap/gui/ExportCloudsDialog.h"
#include "rtabmap/gui/EditDepthArea.h"
#include "rtabmap/gui/EditMapArea.h"
#include "rtabmap/core/SensorData.h"
#include "rtabmap/core/GainCompensator.h"
#include "rtabmap/gui/ExportDialog.h"
#include "rtabmap/gui/ProgressDialog.h"
#include "rtabmap/gui/ParametersToolBox.h"
#include "rtabmap/gui/RecoveryState.h"
#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/io/obj_io.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/common/transforms.h>
#include <pcl/common/common.h>

#ifdef RTABMAP_OCTOMAP
#include "rtabmap/core/OctoMap.h"
#endif

namespace rtabmap {

DatabaseViewer::DatabaseViewer(const QString & ini, QWidget * parent) :
	QMainWindow(parent),
	dbDriver_(0),
	octomap_(0),
	exportDialog_(new ExportCloudsDialog(this)),
	editDepthDialog_(new QDialog(this)),
	editMapDialog_(new QDialog(this)),
	savedMaximized_(false),
	firstCall_(true),
	iniFilePath_(ini),
	infoReducedGraph_(false),
	infoTotalOdom_(0.0),
	infoSessions_(0),
	useLastOptimizedGraphAsGuess_(false)
{
	pathDatabase_ = QDir::homePath()+"/Documents/RTAB-Map"; //use home directory by default

	if(!UDirectory::exists(pathDatabase_.toStdString()))
	{
		pathDatabase_ = QDir::homePath();
	}

	ui_ = new Ui_DatabaseViewer();
	ui_->setupUi(this);
	ui_->buttonBox->setVisible(false);
	connect(ui_->buttonBox->button(QDialogButtonBox::Close), SIGNAL(clicked()), this, SLOT(close()));

	ui_->comboBox_logger_level->setVisible(parent==0);
	ui_->label_logger_level->setVisible(parent==0);
	connect(ui_->comboBox_logger_level, SIGNAL(currentIndexChanged(int)), this, SLOT(updateLoggerLevel()));
	connect(ui_->actionVertical_Layout, SIGNAL(toggled(bool)), this, SLOT(setupMainLayout(bool)));

	editDepthDialog_->resize(640, 480);
	QVBoxLayout * vLayout = new QVBoxLayout(editDepthDialog_);
	editDepthArea_ = new EditDepthArea(editDepthDialog_);
	vLayout->setContentsMargins(0,0,0,0);
	vLayout->setSpacing(0);
	vLayout->addWidget(editDepthArea_, 1);
	QDialogButtonBox * buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, Qt::Horizontal, editDepthDialog_);
	vLayout->addWidget(buttonBox);
	connect(buttonBox, SIGNAL(accepted()), editDepthDialog_, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), editDepthDialog_, SLOT(reject()));
	connect(buttonBox->button(QDialogButtonBox::Reset), SIGNAL(clicked()), editDepthArea_, SLOT(resetChanges()));
	editDepthDialog_->setLayout(vLayout);
	editDepthDialog_->setWindowTitle(tr("Edit Depth Image"));

	editMapDialog_->resize(640, 480);
	vLayout = new QVBoxLayout(editMapDialog_);
	editMapArea_ = new EditMapArea(editMapDialog_);
	vLayout->setContentsMargins(0,0,0,0);
	vLayout->setSpacing(0);
	vLayout->addWidget(editMapArea_, 1);
	buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, Qt::Horizontal, editMapDialog_);
	vLayout->addWidget(buttonBox);
	connect(buttonBox, SIGNAL(accepted()), editMapDialog_, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), editMapDialog_, SLOT(reject()));
	connect(buttonBox->button(QDialogButtonBox::Reset), SIGNAL(clicked()), editMapArea_, SLOT(resetChanges()));
	editMapDialog_->setLayout(vLayout);
	editMapDialog_->setWindowTitle(tr("Edit Optimized Map"));

	QString title("RTAB-Map Database Viewer[*]");
	this->setWindowTitle(title);

	ui_->dockWidget_constraints->setVisible(false);
	ui_->dockWidget_graphView->setVisible(false);
	ui_->dockWidget_occupancyGridView->setVisible(false);
	ui_->dockWidget_guiparameters->setVisible(false);
	ui_->dockWidget_coreparameters->setVisible(false);
	ui_->dockWidget_info->setVisible(false);
	ui_->dockWidget_stereoView->setVisible(false);
	ui_->dockWidget_view3d->setVisible(false);
	ui_->dockWidget_statistics->setVisible(false);

	// Create cloud viewers
	constraintsViewer_ = new CloudViewer(ui_->dockWidgetContents);
	cloudViewer_ = new CloudViewer(ui_->dockWidgetContents_3dviews);
	stereoViewer_ = new CloudViewer(ui_->dockWidgetContents_stereo);
	occupancyGridViewer_ = new CloudViewer(ui_->dockWidgetContents_occupancyGrid);
	constraintsViewer_->setObjectName("constraintsViewer");
	cloudViewer_->setObjectName("cloudViewerA");
	stereoViewer_->setObjectName("stereoViewer");
	occupancyGridViewer_->setObjectName("occupancyGridView");
	ui_->layout_constraintsViewer->addWidget(constraintsViewer_);
	ui_->horizontalLayout_3dviews->addWidget(cloudViewer_, 1);
	ui_->horizontalLayout_stereo->addWidget(stereoViewer_, 1);
	ui_->layout_occupancyGridView->addWidget(occupancyGridViewer_, 1);

	constraintsViewer_->setCameraLockZ(false);
	constraintsViewer_->setCameraFree();
	occupancyGridViewer_->setCameraFree();

	ui_->graphicsView_stereo->setAlpha(255);

#ifndef RTABMAP_OCTOMAP
	ui_->checkBox_octomap->setEnabled(false);
	ui_->checkBox_octomap->setChecked(false);
#endif

	ParametersMap parameters;
	uInsert(parameters, Parameters::getDefaultParameters("SURF"));
	uInsert(parameters, Parameters::getDefaultParameters("SIFT"));
	uInsert(parameters, Parameters::getDefaultParameters("BRIEF"));
	uInsert(parameters, Parameters::getDefaultParameters("FAST"));
	uInsert(parameters, Parameters::getDefaultParameters("GFTT"));
	uInsert(parameters, Parameters::getDefaultParameters("ORB"));
	uInsert(parameters, Parameters::getDefaultParameters("FREAK"));
	uInsert(parameters, Parameters::getDefaultParameters("BRISK"));
	uInsert(parameters, Parameters::getDefaultParameters("Optimizer"));
	uInsert(parameters, Parameters::getDefaultParameters("g2o"));
	uInsert(parameters, Parameters::getDefaultParameters("GTSAM"));
	uInsert(parameters, Parameters::getDefaultParameters("Reg"));
	uInsert(parameters, Parameters::getDefaultParameters("Vis"));
	uInsert(parameters, Parameters::getDefaultParameters("Icp"));
	uInsert(parameters, Parameters::getDefaultParameters("Stereo"));
	uInsert(parameters, Parameters::getDefaultParameters("StereoBM"));
	uInsert(parameters, Parameters::getDefaultParameters("Grid"));
	uInsert(parameters, Parameters::getDefaultParameters("GridGlobal"));
	uInsert(parameters, Parameters::getDefaultParameters("Marker"));
	parameters.insert(*Parameters::getDefaultParameters().find(Parameters::kRGBDOptimizeMaxError()));
	parameters.insert(*Parameters::getDefaultParameters().find(Parameters::kRGBDLoopClosureReextractFeatures()));
	parameters.insert(*Parameters::getDefaultParameters().find(Parameters::kRGBDLoopCovLimited()));
	parameters.insert(*Parameters::getDefaultParameters().find(Parameters::kRGBDProximityPathFilteringRadius()));
	ui_->parameters_toolbox->setupUi(parameters);
	exportDialog_->setObjectName("ExportCloudsDialog");
	restoreDefaultSettings();
	this->readSettings();

	setupMainLayout(ui_->actionVertical_Layout->isChecked());
	ui_->comboBox_octomap_rendering_type->setVisible(ui_->checkBox_octomap->isChecked());
	ui_->spinBox_grid_depth->setVisible(ui_->checkBox_octomap->isChecked());
	ui_->checkBox_grid_empty->setVisible(!ui_->checkBox_octomap->isChecked() || ui_->comboBox_octomap_rendering_type->currentIndex()==0);
	ui_->label_octomap_cubes->setVisible(ui_->checkBox_octomap->isChecked());
	ui_->label_octomap_depth->setVisible(ui_->checkBox_octomap->isChecked());
	ui_->label_octomap_empty->setVisible(!ui_->checkBox_octomap->isChecked() || ui_->comboBox_octomap_rendering_type->currentIndex()==0);

	ui_->menuView->addAction(ui_->dockWidget_constraints->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_graphView->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_occupancyGridView->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_stereoView->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_view3d->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_guiparameters->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_coreparameters->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_info->toggleViewAction());
	ui_->menuView->addAction(ui_->dockWidget_statistics->toggleViewAction());
	connect(ui_->dockWidget_graphView->toggleViewAction(), SIGNAL(triggered()), this, SLOT(updateGraphView()));
	connect(ui_->dockWidget_occupancyGridView->toggleViewAction(), SIGNAL(triggered()), this, SLOT(updateGraphView()));
	connect(ui_->dockWidget_statistics->toggleViewAction(), SIGNAL(triggered()), this, SLOT(updateStatistics()));
	connect(ui_->dockWidget_info->toggleViewAction(), SIGNAL(triggered()), this, SLOT(updateInfo()));


	connect(ui_->parameters_toolbox, SIGNAL(parametersChanged(const QStringList &)), this, SLOT(notifyParametersChanged(const QStringList &)));

	connect(ui_->actionQuit, SIGNAL(triggered()), this, SLOT(close()));

	ui_->actionOpen_database->setEnabled(true);
	ui_->actionClose_database->setEnabled(false);

	// connect actions with custom slots
	ui_->actionSave_config->setShortcut(QKeySequence::Save);
	connect(ui_->actionSave_config, SIGNAL(triggered()), this, SLOT(writeSettings()));
	connect(ui_->actionOpen_database, SIGNAL(triggered()), this, SLOT(openDatabase()));
	connect(ui_->actionClose_database, SIGNAL(triggered()), this, SLOT(closeDatabase()));
	connect(ui_->actionDatabase_recovery, SIGNAL(triggered()), this, SLOT(recoverDatabase()));
	connect(ui_->actionExport, SIGNAL(triggered()), this, SLOT(exportDatabase()));
	connect(ui_->actionExtract_images, SIGNAL(triggered()), this, SLOT(extractImages()));
	connect(ui_->actionEdit_depth_image, SIGNAL(triggered()), this, SLOT(editDepthImage()));
	connect(ui_->actionGenerate_graph_dot, SIGNAL(triggered()), this, SLOT(generateGraph()));
	connect(ui_->actionGenerate_local_graph_dot, SIGNAL(triggered()), this, SLOT(generateLocalGraph()));
	connect(ui_->actionRaw_format_txt, SIGNAL(triggered()), this , SLOT(exportPosesRaw()));
	connect(ui_->actionRGBD_SLAM_format_txt, SIGNAL(triggered()), this , SLOT(exportPosesRGBDSLAM()));
	connect(ui_->actionRGBD_SLAM_motion_capture_format_txt, SIGNAL(triggered()), this , SLOT(exportPosesRGBDSLAMMotionCapture()));
	connect(ui_->actionKITTI_format_txt, SIGNAL(triggered()), this , SLOT(exportPosesKITTI()));
	connect(ui_->actionTORO_graph, SIGNAL(triggered()), this , SLOT(exportPosesTORO()));
	connect(ui_->actionG2o_g2o, SIGNAL(triggered()), this , SLOT(exportPosesG2O()));
	connect(ui_->actionPoses_KML, SIGNAL(triggered()), this , SLOT(exportPosesKML()));
	connect(ui_->actionGPS_TXT, SIGNAL(triggered()), this , SLOT(exportGPS_TXT()));
	connect(ui_->actionGPS_KML, SIGNAL(triggered()), this , SLOT(exportGPS_KML()));
	connect(ui_->actionEdit_optimized_2D_map, SIGNAL(triggered()), this , SLOT(editSaved2DMap()));
	connect(ui_->actionExport_saved_2D_map, SIGNAL(triggered()), this , SLOT(exportSaved2DMap()));
	connect(ui_->actionImport_2D_map, SIGNAL(triggered()), this , SLOT(import2DMap()));
	connect(ui_->actionRegenerate_optimized_2D_map, SIGNAL(triggered()), this , SLOT(regenerateSavedMap()));
	connect(ui_->actionView_optimized_mesh, SIGNAL(triggered()), this , SLOT(viewOptimizedMesh()));
	connect(ui_->actionExport_optimized_mesh, SIGNAL(triggered()), this , SLOT(exportOptimizedMesh()));
	connect(ui_->actionUpdate_optimized_mesh, SIGNAL(triggered()), this , SLOT(updateOptimizedMesh()));
	connect(ui_->actionView_3D_map, SIGNAL(triggered()), this, SLOT(view3DMap()));
	connect(ui_->actionGenerate_3D_map_pcd, SIGNAL(triggered()), this, SLOT(generate3DMap()));
	connect(ui_->actionDetect_more_loop_closures, SIGNAL(triggered()), this, SLOT(detectMoreLoopClosures()));
	connect(ui_->actionUpdate_all_neighbor_covariances, SIGNAL(triggered()), this, SLOT(updateAllNeighborCovariances()));
	connect(ui_->actionUpdate_all_loop_closure_covariances, SIGNAL(triggered()), this, SLOT(updateAllLoopClosureCovariances()));
	connect(ui_->actionRefine_all_neighbor_links, SIGNAL(triggered()), this, SLOT(refineAllNeighborLinks()));
	connect(ui_->actionRefine_all_loop_closure_links, SIGNAL(triggered()), this, SLOT(refineAllLoopClosureLinks()));
	connect(ui_->actionRegenerate_local_grid_maps, SIGNAL(triggered()), this, SLOT(regenerateLocalMaps()));
	connect(ui_->actionRegenerate_local_grid_maps_selected, SIGNAL(triggered()), this, SLOT(regenerateCurrentLocalMaps()));
	connect(ui_->actionReset_all_changes, SIGNAL(triggered()), this, SLOT(resetAllChanges()));
	connect(ui_->actionRestore_default_GUI_settings, SIGNAL(triggered()), this, SLOT(restoreDefaultSettings()));

	//ICP buttons
	connect(ui_->pushButton_refine, SIGNAL(clicked()), this, SLOT(refineConstraint()));
	connect(ui_->pushButton_add, SIGNAL(clicked()), this, SLOT(addConstraint()));
	connect(ui_->pushButton_reset, SIGNAL(clicked()), this, SLOT(resetConstraint()));
	connect(ui_->pushButton_reject, SIGNAL(clicked()), this, SLOT(rejectConstraint()));
	ui_->pushButton_refine->setEnabled(false);
	ui_->pushButton_add->setEnabled(false);
	ui_->pushButton_reset->setEnabled(false);
	ui_->pushButton_reject->setEnabled(false);

	ui_->menuEdit->setEnabled(false);
	ui_->actionGenerate_3D_map_pcd->setEnabled(false);
	ui_->actionExport->setEnabled(false);
	ui_->actionExtract_images->setEnabled(false);
	ui_->menuExport_poses->setEnabled(false);
	ui_->menuExport_GPS->setEnabled(false);
	ui_->actionPoses_KML->setEnabled(false);
	ui_->actionEdit_optimized_2D_map->setEnabled(false);
	ui_->actionExport_saved_2D_map->setEnabled(false);
	ui_->actionImport_2D_map->setEnabled(false);
	ui_->actionRegenerate_optimized_2D_map->setEnabled(false);
	ui_->actionView_optimized_mesh->setEnabled(false);
	ui_->actionExport_optimized_mesh->setEnabled(false);
	ui_->actionUpdate_optimized_mesh->setEnabled(false);

	ui_->horizontalSlider_A->setTracking(false);
	ui_->horizontalSlider_B->setTracking(false);
	ui_->horizontalSlider_A->setEnabled(false);
	ui_->horizontalSlider_B->setEnabled(false);
	connect(ui_->horizontalSlider_A, SIGNAL(valueChanged(int)), this, SLOT(sliderAValueChanged(int)));
	connect(ui_->horizontalSlider_B, SIGNAL(valueChanged(int)), this, SLOT(sliderBValueChanged(int)));
	connect(ui_->horizontalSlider_A, SIGNAL(sliderMoved(int)), this, SLOT(sliderAMoved(int)));
	connect(ui_->horizontalSlider_B, SIGNAL(sliderMoved(int)), this, SLOT(sliderBMoved(int)));

	connect(ui_->spinBox_mesh_angleTolerance, SIGNAL(valueChanged(int)), this, SLOT(update3dView()));
	connect(ui_->spinBox_mesh_minClusterSize, SIGNAL(valueChanged(int)), this, SLOT(update3dView()));
	connect(ui_->spinBox_mesh_fillDepthHoles, SIGNAL(valueChanged(int)), this, SLOT(update3dView()));
	connect(ui_->spinBox_mesh_depthError, SIGNAL(valueChanged(int)), this, SLOT(update3dView()));
	connect(ui_->checkBox_mesh_quad, SIGNAL(toggled(bool)), this, SLOT(update3dView()));
	connect(ui_->spinBox_mesh_triangleSize, SIGNAL(valueChanged(int)), this, SLOT(update3dView()));
	connect(ui_->checkBox_showWords, SIGNAL(toggled(bool)), this, SLOT(update3dView()));
	connect(ui_->checkBox_showCloud, SIGNAL(toggled(bool)), this, SLOT(update3dView()));
	connect(ui_->checkBox_showMesh, SIGNAL(toggled(bool)), this, SLOT(update3dView()));
	connect(ui_->checkBox_showScan, SIGNAL(toggled(bool)), this, SLOT(update3dView()));
	connect(ui_->checkBox_showMap, SIGNAL(toggled(bool)), this, SLOT(update3dView()));
	connect(ui_->checkBox_showGrid, SIGNAL(toggled(bool)), this, SLOT(update3dView()));
	connect(ui_->checkBox_odomFrame_3dview, SIGNAL(toggled(bool)), this, SLOT(update3dView()));

	ui_->horizontalSlider_neighbors->setTracking(false);
	ui_->horizontalSlider_loops->setTracking(false);
	ui_->horizontalSlider_neighbors->setEnabled(false);
	ui_->horizontalSlider_loops->setEnabled(false);
	connect(ui_->horizontalSlider_neighbors, SIGNAL(valueChanged(int)), this, SLOT(sliderNeighborValueChanged(int)));
	connect(ui_->horizontalSlider_loops, SIGNAL(valueChanged(int)), this, SLOT(sliderLoopValueChanged(int)));
	connect(ui_->horizontalSlider_neighbors, SIGNAL(sliderMoved(int)), this, SLOT(sliderNeighborValueChanged(int)));
	connect(ui_->horizontalSlider_loops, SIGNAL(sliderMoved(int)), this, SLOT(sliderLoopValueChanged(int)));
	connect(ui_->checkBox_showOptimized, SIGNAL(stateChanged(int)), this, SLOT(updateConstraintView()));
	connect(ui_->checkBox_show3Dclouds, SIGNAL(stateChanged(int)), this, SLOT(updateConstraintView()));
	connect(ui_->checkBox_show2DScans, SIGNAL(stateChanged(int)), this, SLOT(updateConstraintView()));
	connect(ui_->checkBox_show3DWords, SIGNAL(stateChanged(int)), this, SLOT(updateConstraintView()));
	connect(ui_->checkBox_odomFrame, SIGNAL(stateChanged(int)), this, SLOT(updateConstraintView()));
	ui_->checkBox_showOptimized->setEnabled(false);

	ui_->horizontalSlider_iterations->setTracking(false);
	ui_->horizontalSlider_iterations->setEnabled(false);
	ui_->spinBox_optimizationsFrom->setEnabled(false);
	connect(ui_->horizontalSlider_iterations, SIGNAL(valueChanged(int)), this, SLOT(sliderIterationsValueChanged(int)));
	connect(ui_->horizontalSlider_iterations, SIGNAL(sliderMoved(int)), this, SLOT(sliderIterationsValueChanged(int)));
	connect(ui_->spinBox_optimizationsFrom, SIGNAL(editingFinished()), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_iterativeOptimization, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_spanAllMaps, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_wmState, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->graphViewer, SIGNAL(mapShownRequested()), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_ignorePoseCorrection, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_ignorePoseCorrection, SIGNAL(stateChanged(int)), this, SLOT(updateConstraintView()));
	connect(ui_->checkBox_ignoreGlobalLoop, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_ignoreLocalLoopSpace, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_ignoreLocalLoopTime, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_ignoreUserLoop, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->doubleSpinBox_optimizationScale, SIGNAL(editingFinished()), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_octomap, SIGNAL(stateChanged(int)), this, SLOT(updateGrid()));
	connect(ui_->checkBox_grid_2d, SIGNAL(stateChanged(int)), this, SLOT(updateGrid()));
	connect(ui_->comboBox_octomap_rendering_type, SIGNAL(currentIndexChanged(int)), this, SLOT(updateOctomapView()));
	connect(ui_->spinBox_grid_depth, SIGNAL(valueChanged(int)), this, SLOT(updateOctomapView()));
	connect(ui_->checkBox_grid_empty, SIGNAL(stateChanged(int)), this, SLOT(updateGrid()));
	connect(ui_->doubleSpinBox_gainCompensationRadius, SIGNAL(valueChanged(double)), this, SLOT(updateConstraintView()));
	connect(ui_->doubleSpinBox_voxelSize, SIGNAL(valueChanged(double)), this, SLOT(updateConstraintView()));
	connect(ui_->doubleSpinBox_voxelSize, SIGNAL(valueChanged(double)), this, SLOT(update3dView()));
	connect(ui_->spinBox_decimation, SIGNAL(valueChanged(int)), this, SLOT(updateConstraintView()));
	connect(ui_->spinBox_decimation, SIGNAL(valueChanged(int)), this, SLOT(update3dView()));
	connect(ui_->groupBox_posefiltering, SIGNAL(clicked(bool)), this, SLOT(updateGraphView()));
	connect(ui_->doubleSpinBox_posefilteringRadius, SIGNAL(editingFinished()), this, SLOT(updateGraphView()));
	connect(ui_->doubleSpinBox_posefilteringAngle, SIGNAL(editingFinished()), this, SLOT(updateGraphView()));

	ui_->label_stereo_inliers_name->setStyleSheet("QLabel {color : blue; }");
	ui_->label_stereo_flowOutliers_name->setStyleSheet("QLabel {color : red; }");
	ui_->label_stereo_slopeOutliers_name->setStyleSheet("QLabel {color : yellow; }");
	ui_->label_stereo_disparityOutliers_name->setStyleSheet("QLabel {color : magenta; }");


	// connect configuration changed
	connect(ui_->graphViewer, SIGNAL(configChanged()), this, SLOT(configModified()));
	//connect(ui_->graphicsView_A, SIGNAL(configChanged()), this, SLOT(configModified()));
	//connect(ui_->graphicsView_B, SIGNAL(configChanged()), this, SLOT(configModified()));
	connect(ui_->comboBox_logger_level, SIGNAL(currentIndexChanged(int)), this, SLOT(configModified()));
	connect(ui_->actionVertical_Layout, SIGNAL(toggled(bool)), this, SLOT(configModified()));
	connect(ui_->checkBox_alignPosesWithGroundTruth, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_alignScansCloudsWithGroundTruth, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_ignoreIntermediateNodes, SIGNAL(stateChanged(int)), this, SLOT(configModified()));
	connect(ui_->checkBox_ignoreIntermediateNodes, SIGNAL(stateChanged(int)), this, SLOT(updateGraphView()));
	connect(ui_->checkBox_timeStats, SIGNAL(stateChanged(int)), this, SLOT(configModified()));
	connect(ui_->checkBox_timeStats, SIGNAL(stateChanged(int)), this, SLOT(updateStatistics()));
	// Graph view
	connect(ui_->doubleSpinBox_gainCompensationRadius, SIGNAL(valueChanged(double)), this, SLOT(configModified()));
	connect(ui_->doubleSpinBox_voxelSize, SIGNAL(valueChanged(double)), this, SLOT(configModified()));
	connect(ui_->spinBox_decimation, SIGNAL(valueChanged(int)), this, SLOT(configModified()));
	connect(ui_->groupBox_posefiltering, SIGNAL(clicked(bool)), this, SLOT(configModified()));
	connect(ui_->doubleSpinBox_posefilteringRadius, SIGNAL(valueChanged(double)), this, SLOT(configModified()));
	connect(ui_->doubleSpinBox_posefilteringAngle, SIGNAL(valueChanged(double)), this, SLOT(configModified()));

	connect(ui_->spinBox_icp_decimation, SIGNAL(valueChanged(int)), this, SLOT(configModified()));
	connect(ui_->doubleSpinBox_icp_maxDepth, SIGNAL(valueChanged(double)), this, SLOT(configModified()));
	connect(ui_->doubleSpinBox_icp_minDepth, SIGNAL(valueChanged(double)), this, SLOT(configModified()));
	connect(ui_->checkBox_icp_from_depth, SIGNAL(stateChanged(int)), this, SLOT(configModified()));
	
	connect(ui_->doubleSpinBox_detectMore_radius, SIGNAL(valueChanged(double)), this, SLOT(configModified()));
	connect(ui_->doubleSpinBox_detectMore_angle, SIGNAL(valueChanged(double)), this, SLOT(configModified()));
	connect(ui_->spinBox_detectMore_iterations, SIGNAL(valueChanged(int)), this, SLOT(configModified()));
	connect(ui_->checkBox_detectMore_intraSession, SIGNAL(stateChanged(int)), this, SLOT(configModified()));
	connect(ui_->checkBox_detectMore_interSession, SIGNAL(stateChanged(int)), this, SLOT(configModified()));

	connect(ui_->lineEdit_obstacleColor, SIGNAL(textChanged(const QString &)), this, SLOT(configModified()));
	connect(ui_->lineEdit_groundColor, SIGNAL(textChanged(const QString &)), this, SLOT(configModified()));
	connect(ui_->lineEdit_emptyColor, SIGNAL(textChanged(const QString &)), this, SLOT(configModified()));
	connect(ui_->lineEdit_obstacleColor, SIGNAL(textChanged(const QString &)), this, SLOT(updateGrid()));
	connect(ui_->lineEdit_groundColor, SIGNAL(textChanged(const QString &)), this, SLOT(updateGrid()));
	connect(ui_->lineEdit_emptyColor, SIGNAL(textChanged(const QString &)), this, SLOT(updateGrid()));
	connect(ui_->toolButton_obstacleColor, SIGNAL(clicked(bool)), this, SLOT(selectObstacleColor()));
	connect(ui_->toolButton_groundColor, SIGNAL(clicked(bool)), this, SLOT(selectGroundColor()));
	connect(ui_->toolButton_emptyColor, SIGNAL(clicked(bool)), this, SLOT(selectEmptyColor()));

	connect(exportDialog_, SIGNAL(configChanged()), this, SLOT(configModified()));

	// dockwidget
	QList<QDockWidget*> dockWidgets = this->findChildren<QDockWidget*>();
	for(int i=0; i<dockWidgets.size(); ++i)
	{
		connect(dockWidgets[i], SIGNAL(dockLocationChanged(Qt::DockWidgetArea)), this, SLOT(configModified()));
		connect(dockWidgets[i]->toggleViewAction(), SIGNAL(toggled(bool)), this, SLOT(configModified()));
	}
	ui_->dockWidget_constraints->installEventFilter(this);
	ui_->dockWidget_graphView->installEventFilter(this);
	ui_->dockWidget_occupancyGridView->installEventFilter(this);
	ui_->dockWidget_stereoView->installEventFilter(this);
	ui_->dockWidget_view3d->installEventFilter(this);
	ui_->dockWidget_guiparameters->installEventFilter(this);
	ui_->dockWidget_coreparameters->installEventFilter(this);
	ui_->dockWidget_info->installEventFilter(this);
	ui_->dockWidget_statistics->installEventFilter(this);
}

DatabaseViewer::~DatabaseViewer()
{
	delete ui_;
	delete dbDriver_;
#ifdef RTABMAP_OCTOMAP
	delete octomap_;
#endif
}

void DatabaseViewer::setupMainLayout(bool vertical)
{
	if(vertical)
	{
		qobject_cast<QHBoxLayout *>(ui_->horizontalLayout_imageViews->layout())->setDirection(QBoxLayout::TopToBottom);
	}
	else if(!vertical)
	{
		qobject_cast<QHBoxLayout *>(ui_->horizontalLayout_imageViews->layout())->setDirection(QBoxLayout::LeftToRight);
	}
	if(ids_.size())
	{
		sliderAValueChanged(ui_->horizontalSlider_A->value()); // update matching lines
	}
}

void DatabaseViewer::showCloseButton(bool visible)
{
	ui_->buttonBox->setVisible(visible);
}

void DatabaseViewer::configModified()
{
	this->setWindowModified(true);
}

QString DatabaseViewer::getIniFilePath() const
{
	if(!iniFilePath_.isEmpty())
	{
		return iniFilePath_;
	}
	QString privatePath = QDir::homePath() + "/.rtabmap";
	if(!QDir(privatePath).exists())
	{
		QDir::home().mkdir(".rtabmap");
	}
	return privatePath + "/rtabmap.ini";
}

void DatabaseViewer::readSettings()
{
	QString path = getIniFilePath();
	QSettings settings(path, QSettings::IniFormat);
	settings.beginGroup("DatabaseViewer");

	//load window state / geometry
	QByteArray bytes;
	bytes = settings.value("geometry", QByteArray()).toByteArray();
	if(!bytes.isEmpty())
	{
		this->restoreGeometry(bytes);
	}
	bytes = settings.value("state", QByteArray()).toByteArray();
	if(!bytes.isEmpty())
	{
		this->restoreState(bytes);
	}
	savedMaximized_ = settings.value("maximized", false).toBool();

	ui_->comboBox_logger_level->setCurrentIndex(settings.value("loggerLevel", ui_->comboBox_logger_level->currentIndex()).toInt());
	ui_->actionVertical_Layout->setChecked(settings.value("verticalLayout", ui_->actionVertical_Layout->isChecked()).toBool());
	ui_->checkBox_ignoreIntermediateNodes->setChecked(settings.value("ignoreIntermediateNodes", ui_->checkBox_ignoreIntermediateNodes->isChecked()).toBool());
	ui_->checkBox_timeStats->setChecked(settings.value("timeStats", ui_->checkBox_timeStats->isChecked()).toBool());

	// GraphViewer settings
	ui_->graphViewer->loadSettings(settings, "GraphView");

	settings.beginGroup("optimization");
	ui_->doubleSpinBox_gainCompensationRadius->setValue(settings.value("gainCompensationRadius", ui_->doubleSpinBox_gainCompensationRadius->value()).toDouble());
	ui_->doubleSpinBox_voxelSize->setValue(settings.value("voxelSize", ui_->doubleSpinBox_voxelSize->value()).toDouble());
	ui_->spinBox_decimation->setValue(settings.value("decimation", ui_->spinBox_decimation->value()).toInt());
	settings.endGroup();

	settings.beginGroup("grid");
	ui_->groupBox_posefiltering->setChecked(settings.value("poseFiltering", ui_->groupBox_posefiltering->isChecked()).toBool());
	ui_->doubleSpinBox_posefilteringRadius->setValue(settings.value("poseFilteringRadius", ui_->doubleSpinBox_posefilteringRadius->value()).toDouble());
	ui_->doubleSpinBox_posefilteringAngle->setValue(settings.value("poseFilteringAngle", ui_->doubleSpinBox_posefilteringAngle->value()).toDouble());
	ui_->lineEdit_obstacleColor->setText(settings.value("colorObstacle", ui_->lineEdit_obstacleColor->text()).toString());
	ui_->lineEdit_groundColor->setText(settings.value("colorGround", ui_->lineEdit_groundColor->text()).toString());
	ui_->lineEdit_emptyColor->setText(settings.value("colorEmpty", ui_->lineEdit_emptyColor->text()).toString());
	settings.endGroup();

	settings.beginGroup("mesh");
	ui_->checkBox_mesh_quad->setChecked(settings.value("quad", ui_->checkBox_mesh_quad->isChecked()).toBool());
	ui_->spinBox_mesh_angleTolerance->setValue(settings.value("angleTolerance", ui_->spinBox_mesh_angleTolerance->value()).toInt());
	ui_->spinBox_mesh_minClusterSize->setValue(settings.value("minClusterSize", ui_->spinBox_mesh_minClusterSize->value()).toInt());
	ui_->spinBox_mesh_fillDepthHoles->setValue(settings.value("fillDepthHolesSize", ui_->spinBox_mesh_fillDepthHoles->value()).toInt());
	ui_->spinBox_mesh_depthError->setValue(settings.value("fillDepthHolesError", ui_->spinBox_mesh_depthError->value()).toInt());
	ui_->spinBox_mesh_triangleSize->setValue(settings.value("triangleSize", ui_->spinBox_mesh_triangleSize->value()).toInt());
	settings.endGroup();

	// ImageViews
	//ui_->graphicsView_A->loadSettings(settings, "ImageViewA");
	//ui_->graphicsView_B->loadSettings(settings, "ImageViewB");

	// ICP parameters
	settings.beginGroup("icp");
	ui_->spinBox_icp_decimation->setValue(settings.value("decimation", ui_->spinBox_icp_decimation->value()).toInt());
	ui_->doubleSpinBox_icp_maxDepth->setValue(settings.value("maxDepth", ui_->doubleSpinBox_icp_maxDepth->value()).toDouble());
	ui_->doubleSpinBox_icp_minDepth->setValue(settings.value("minDepth", ui_->doubleSpinBox_icp_minDepth->value()).toDouble());
	ui_->checkBox_icp_from_depth->setChecked(settings.value("icpFromDepth", ui_->checkBox_icp_from_depth->isChecked()).toBool());
	settings.endGroup();

	settings.endGroup(); // DatabaseViewer

	// Use same parameters used by RTAB-Map
	settings.beginGroup("Gui");
	exportDialog_->loadSettings(settings, exportDialog_->objectName());
	settings.beginGroup("PostProcessingDialog");
	ui_->doubleSpinBox_detectMore_radius->setValue(settings.value("cluster_radius", ui_->doubleSpinBox_detectMore_radius->value()).toDouble());
	ui_->doubleSpinBox_detectMore_angle->setValue(settings.value("cluster_angle", ui_->doubleSpinBox_detectMore_angle->value()).toDouble());
	ui_->spinBox_detectMore_iterations->setValue(settings.value("iterations", ui_->spinBox_detectMore_iterations->value()).toInt());
	ui_->checkBox_detectMore_intraSession->setChecked(settings.value("intra_session", ui_->checkBox_detectMore_intraSession->isChecked()).toBool());
	ui_->checkBox_detectMore_interSession->setChecked(settings.value("inter_session", ui_->checkBox_detectMore_interSession->isChecked()).toBool());
	settings.endGroup();
	settings.endGroup();


	ParametersMap parameters;
	Parameters::readINI(path.toStdString(), parameters);
	for(ParametersMap::iterator iter = parameters.begin(); iter!= parameters.end(); ++iter)
	{
		ui_->parameters_toolbox->updateParameter(iter->first, iter->second);
	}
}

void DatabaseViewer::writeSettings()
{
	QString path = getIniFilePath();
	QSettings settings(path, QSettings::IniFormat);
	settings.beginGroup("DatabaseViewer");

	//save window state / geometry
	if(!this->isMaximized())
	{
		settings.setValue("geometry", this->saveGeometry());
	}
	settings.setValue("state", this->saveState());
	settings.setValue("maximized", this->isMaximized());
	savedMaximized_ = this->isMaximized();

	settings.setValue("loggerLevel", ui_->comboBox_logger_level->currentIndex());
	settings.setValue("verticalLayout", ui_->actionVertical_Layout->isChecked());
	settings.setValue("ignoreIntermediateNodes", ui_->checkBox_ignoreIntermediateNodes->isChecked());
	settings.setValue("timeStats", ui_->checkBox_timeStats->isChecked());

	// save GraphViewer settings
	ui_->graphViewer->saveSettings(settings, "GraphView");

	// save optimization settings
	settings.beginGroup("optimization");
	settings.setValue("gainCompensationRadius", ui_->doubleSpinBox_gainCompensationRadius->value());
	settings.setValue("voxelSize", ui_->doubleSpinBox_voxelSize->value());
	settings.setValue("decimation", ui_->spinBox_decimation->value());
	settings.endGroup();

	// save Grid settings
	settings.beginGroup("grid");
	settings.setValue("poseFiltering", ui_->groupBox_posefiltering->isChecked());
	settings.setValue("poseFilteringRadius", ui_->doubleSpinBox_posefilteringRadius->value());
	settings.setValue("poseFilteringAngle", ui_->doubleSpinBox_posefilteringAngle->value());
	settings.setValue("colorObstacle", ui_->lineEdit_obstacleColor->text());
	settings.setValue("colorGround", ui_->lineEdit_groundColor->text());
	settings.setValue("colorEmpty", ui_->lineEdit_emptyColor->text());
	settings.endGroup();

	settings.beginGroup("mesh");
	settings.setValue("quad", ui_->checkBox_mesh_quad->isChecked());
	settings.setValue("angleTolerance", ui_->spinBox_mesh_angleTolerance->value());
	settings.setValue("minClusterSize", ui_->spinBox_mesh_minClusterSize->value());
	settings.setValue("fillDepthHolesSize", ui_->spinBox_mesh_fillDepthHoles->value());
	settings.setValue("fillDepthHolesError", ui_->spinBox_mesh_depthError->value());
	settings.setValue("triangleSize", ui_->spinBox_mesh_triangleSize->value());
	settings.endGroup();

	// ImageViews
	//ui_->graphicsView_A->saveSettings(settings, "ImageViewA");
	//ui_->graphicsView_B->saveSettings(settings, "ImageViewB");

	// save ICP parameters
	settings.beginGroup("icp");
	settings.setValue("decimation", ui_->spinBox_icp_decimation->value());
	settings.setValue("maxDepth", ui_->doubleSpinBox_icp_maxDepth->value());
	settings.setValue("minDepth", ui_->doubleSpinBox_icp_minDepth->value());
	settings.setValue("icpFromDepth", ui_->checkBox_icp_from_depth->isChecked());
	settings.endGroup();
	
	settings.endGroup(); // DatabaseViewer

	// Use same parameters used by RTAB-Map
	settings.beginGroup("Gui");
	exportDialog_->saveSettings(settings, exportDialog_->objectName());
	settings.beginGroup("PostProcessingDialog");
	settings.setValue("cluster_radius",  ui_->doubleSpinBox_detectMore_radius->value());
	settings.setValue("cluster_angle", ui_->doubleSpinBox_detectMore_angle->value());
	settings.setValue("iterations", ui_->spinBox_detectMore_iterations->value());
	settings.setValue("intra_session", ui_->checkBox_detectMore_intraSession->isChecked());
	settings.setValue("inter_session", ui_->checkBox_detectMore_interSession->isChecked());
	settings.endGroup();
	settings.endGroup();

	ParametersMap parameters = ui_->parameters_toolbox->getParameters();
	for(ParametersMap::iterator iter=parameters.begin(); iter!=parameters.end();)
	{
		if(!ui_->parameters_toolbox->getParameterWidget(iter->first.c_str()))
		{
			parameters.erase(iter++);
		}
		else
		{
			++iter;
		}
	}
	Parameters::writeINI(path.toStdString(), parameters);

	this->setWindowModified(false);
}

void DatabaseViewer::restoreDefaultSettings()
{
	// reset GUI parameters
	ui_->comboBox_logger_level->setCurrentIndex(1);
	ui_->checkBox_alignPosesWithGroundTruth->setChecked(true);
	ui_->checkBox_alignScansCloudsWithGroundTruth->setChecked(false);
	ui_->checkBox_ignoreIntermediateNodes->setChecked(false);
	ui_->checkBox_timeStats->setChecked(true);

	ui_->checkBox_iterativeOptimization->setChecked(true);
	ui_->checkBox_spanAllMaps->setChecked(true);
	ui_->checkBox_wmState->setChecked(false);
	ui_->checkBox_ignorePoseCorrection->setChecked(false);
	ui_->checkBox_ignoreGlobalLoop->setChecked(false);
	ui_->checkBox_ignoreLocalLoopSpace->setChecked(false);
	ui_->checkBox_ignoreLocalLoopTime->setChecked(false);
	ui_->checkBox_ignoreUserLoop->setChecked(false);
	ui_->doubleSpinBox_optimizationScale->setValue(1.0);
	ui_->doubleSpinBox_gainCompensationRadius->setValue(0.0);
	ui_->doubleSpinBox_voxelSize->setValue(0.0);
	ui_->spinBox_decimation->setValue(1);

	ui_->groupBox_posefiltering->setChecked(false);
	ui_->doubleSpinBox_posefilteringRadius->setValue(0.1);
	ui_->doubleSpinBox_posefilteringAngle->setValue(30);
	ui_->checkBox_grid_empty->setChecked(true);
	ui_->checkBox_octomap->setChecked(false);
	ui_->lineEdit_obstacleColor->setText(QColor(Qt::red).name());
	ui_->lineEdit_groundColor->setText(QColor(Qt::green).name());
	ui_->lineEdit_emptyColor->setText(QColor(Qt::yellow).name());

	ui_->checkBox_mesh_quad->setChecked(true);
	ui_->spinBox_mesh_angleTolerance->setValue(15);
	ui_->spinBox_mesh_minClusterSize->setValue(0);
	ui_->spinBox_mesh_fillDepthHoles->setValue(false);
	ui_->spinBox_mesh_depthError->setValue(10);
	ui_->spinBox_mesh_triangleSize->setValue(2);

	ui_->spinBox_icp_decimation->setValue(1);
	ui_->doubleSpinBox_icp_maxDepth->setValue(0.0);
	ui_->doubleSpinBox_icp_minDepth->setValue(0.0);
	ui_->checkBox_icp_from_depth->setChecked(false);

	ui_->doubleSpinBox_detectMore_radius->setValue(1.0);
	ui_->doubleSpinBox_detectMore_angle->setValue(30.0);
	ui_->spinBox_detectMore_iterations->setValue(5);
	ui_->checkBox_detectMore_intraSession->setChecked(true);
	ui_->checkBox_detectMore_interSession->setChecked(true);
}

void DatabaseViewer::openDatabase()
{
	QString path = QFileDialog::getOpenFileName(this, tr("Select file"), pathDatabase_, tr("Databases (*.db)"));
	if(!path.isEmpty())
	{
		openDatabase(path);
	}
}

bool DatabaseViewer::openDatabase(const QString & path)
{
	UDEBUG("Open database \"%s\"", path.toStdString().c_str());
	if(QFile::exists(path))
	{
		if(QFileInfo(path).isFile())
		{
			std::string driverType = "sqlite3";

			dbDriver_ = DBDriver::create();

			if(!dbDriver_->openConnection(path.toStdString()))
			{
				ui_->actionClose_database->setEnabled(false);
				ui_->actionOpen_database->setEnabled(true);
				delete dbDriver_;
				dbDriver_ = 0;
				QMessageBox::warning(this, "Database error", tr("Can't open database \"%1\"").arg(path));
			}
			else
			{
				ui_->actionClose_database->setEnabled(true);
				ui_->actionOpen_database->setEnabled(false);

				pathDatabase_ = UDirectory::getDir(path.toStdString()).c_str();
				if(pathDatabase_.isEmpty() || pathDatabase_.compare(".") == 0)
				{
					pathDatabase_ = QDir::currentPath();
				}
				databaseFileName_ = UFile::getName(path.toStdString());
				ui_->graphViewer->setWorkingDirectory(pathDatabase_);

				// look if there are saved parameters
				ParametersMap parameters = dbDriver_->getLastParameters();

				if(parameters.size())
				{
					const ParametersMap & currentParameters = ui_->parameters_toolbox->getParameters();
					ParametersMap differentParameters;
					for(ParametersMap::iterator iter=parameters.begin(); iter!=parameters.end(); ++iter)
					{
						ParametersMap::const_iterator jter = currentParameters.find(iter->first);
						if(jter!=currentParameters.end() &&
						   ui_->parameters_toolbox->getParameterWidget(QString(iter->first.c_str())) != 0 &&
						   iter->second.compare(jter->second) != 0 &&
						   iter->first.compare(Parameters::kRtabmapWorkingDirectory()) != 0)
						{
							bool different = true;
							if(Parameters::getType(iter->first).compare("double") ==0 ||
							   Parameters::getType(iter->first).compare("float") == 0)
							{
								if(uStr2Double(iter->second) == uStr2Double(jter->second))
								{
									different = false;
								}
							}
							if(different)
							{
								differentParameters.insert(*iter);
								QString msg = tr("Parameter \"%1\": database=\"%2\" Preferences=\"%3\"")
										.arg(iter->first.c_str())
										.arg(iter->second.c_str())
										.arg(jter->second.c_str());
								UWARN(msg.toStdString().c_str());
							}
						}
					}

					if(differentParameters.size())
					{
						int r = QMessageBox::question(this,
								tr("Update parameters..."),
								tr("The database is using %1 different parameter(s) than "
								   "those currently set in Core parameters panel. Do you want "
								   "to use database's parameters?").arg(differentParameters.size()),
								QMessageBox::Yes | QMessageBox::No,
								QMessageBox::Yes);
						if(r == QMessageBox::Yes)
						{
							QStringList str;
							for(rtabmap::ParametersMap::const_iterator iter = differentParameters.begin(); iter!=differentParameters.end(); ++iter)
							{
								ui_->parameters_toolbox->updateParameter(iter->first, iter->second);
								str.push_back(iter->first.c_str());
							}
							notifyParametersChanged(str);
						}
					}
				}

				updateIds();
				return true;
			}
		}
		else // directory
		{
			pathDatabase_ = path;
			if(pathDatabase_.isEmpty() || pathDatabase_.compare(".") == 0)
			{
				pathDatabase_ = QDir::currentPath();
			}
			ui_->graphViewer->setWorkingDirectory(pathDatabase_);
		}
	}
	else
	{
		QMessageBox::warning(this, "Database error", tr("Database \"%1\" does not exist.").arg(path));
	}
	return false;
}

bool DatabaseViewer::closeDatabase()
{
	if(dbDriver_)
	{
		if(linksAdded_.size() || linksRefined_.size() || linksRemoved_.size())
		{
			QMessageBox::StandardButton button = QMessageBox::question(this,
					tr("Links modified"),
					tr("Some links are modified (%1 added, %2 refined, %3 removed), do you want to save them?")
					.arg(linksAdded_.size()).arg(linksRefined_.size()).arg(linksRemoved_.size()),
					QMessageBox::Cancel | QMessageBox::Yes | QMessageBox::No,
					QMessageBox::Cancel);

			if(button == QMessageBox::Yes)
			{
				// Added links
				for(std::multimap<int, rtabmap::Link>::iterator iter=linksAdded_.begin(); iter!=linksAdded_.end(); ++iter)
				{
					std::multimap<int, rtabmap::Link>::iterator refinedIter = rtabmap::graph::findLink(linksRefined_, iter->second.from(), iter->second.to());
					if(refinedIter != linksRefined_.end())
					{
						dbDriver_->addLink(refinedIter->second);
						dbDriver_->addLink(refinedIter->second.inverse());
					}
					else
					{
						dbDriver_->addLink(iter->second);
						dbDriver_->addLink(iter->second.inverse());
					}
				}

				//Refined links
				for(std::multimap<int, rtabmap::Link>::iterator iter=linksRefined_.begin(); iter!=linksRefined_.end(); ++iter)
				{
					if(!containsLink(linksAdded_, iter->second.from(), iter->second.to()))
					{
						dbDriver_->updateLink(iter->second);
						dbDriver_->updateLink(iter->second.inverse());
					}
				}

				// Rejected links
				for(std::multimap<int, rtabmap::Link>::iterator iter=linksRemoved_.begin(); iter!=linksRemoved_.end(); ++iter)
				{
					dbDriver_->removeLink(iter->second.to(), iter->second.from());
					dbDriver_->removeLink(iter->second.from(), iter->second.to());
				}
				linksAdded_.clear();
				linksRefined_.clear();
				linksRemoved_.clear();

				// Clear the optimized poses, this will force rtabmap to re-optimize the graph on initialization
				Transform lastLocalizationPose;
				if(!dbDriver_->loadOptimizedPoses(&lastLocalizationPose).empty())
				{
					dbDriver_->saveOptimizedPoses(std::map<int, Transform>(), lastLocalizationPose);
				}
				// This will force rtabmap_ros to regenerate the global occupancy grid if there was one
				dbDriver_->save2DMap(cv::Mat(), 0, 0, 0);
			}

			if(button != QMessageBox::Yes && button != QMessageBox::No)
			{
				return false;
			}
		}

		if(	generatedLocalMaps_.size() &&
			uStrNumCmp(dbDriver_->getDatabaseVersion(), "0.11.10") >= 0)
		{
			QMessageBox::StandardButton button = QMessageBox::question(this,
					tr("Local occupancy grid maps modified"),
					tr("%1 occupancy grid maps are modified, do you want to "
						"save them? This will overwrite occupancy grids saved in the database.")
					.arg(generatedLocalMaps_.size()),
					QMessageBox::Cancel | QMessageBox::Yes | QMessageBox::No,
					QMessageBox::Cancel);

			if(button == QMessageBox::Yes)
			{
				UASSERT(generatedLocalMaps_.size() == generatedLocalMapsInfo_.size());
				std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat > >::iterator mapIter = generatedLocalMaps_.begin();
				std::map<int, std::pair<float, cv::Point3f> >::iterator infoIter = generatedLocalMapsInfo_.begin();
				for(; mapIter!=generatedLocalMaps_.end(); ++mapIter, ++infoIter)
				{
					UASSERT(mapIter->first == infoIter->first);
					dbDriver_->updateOccupancyGrid(
							mapIter->first,
							mapIter->second.first.first,
							mapIter->second.first.second,
							mapIter->second.second,
							infoIter->second.first,
							infoIter->second.second);
				}
				generatedLocalMaps_.clear();
				generatedLocalMapsInfo_.clear();
				localMaps_.clear();
				localMapsInfo_.clear();

				// This will force rtabmap_ros to regenerate the global occupancy grid if there was one
				dbDriver_->save2DMap(cv::Mat(), 0, 0, 0);
			}

			if(button != QMessageBox::Yes && button != QMessageBox::No)
			{
				return false;
			}
		}

		delete dbDriver_;
		dbDriver_ = 0;
		ids_.clear();
		idToIndex_.clear();
		neighborLinks_.clear();
		loopLinks_.clear();
		graphes_.clear();
		graphLinks_.clear();
		odomPoses_.clear();
		groundTruthPoses_.clear();
		gpsPoses_.clear();
		gpsValues_.clear();
		lastWmIds_.clear();
		mapIds_.clear();
		weights_.clear();
		wmStates_.clear();
		links_.clear();
		linksAdded_.clear();
		linksRefined_.clear();
		linksRemoved_.clear();
		localMaps_.clear();
		localMapsInfo_.clear();
		generatedLocalMaps_.clear();
		generatedLocalMapsInfo_.clear();
		ui_->graphViewer->clearAll();
		occupancyGridViewer_->clear();
		ui_->menuEdit->setEnabled(false);
		ui_->actionGenerate_3D_map_pcd->setEnabled(false);
		ui_->actionExport->setEnabled(false);
		ui_->actionExtract_images->setEnabled(false);
		ui_->menuExport_poses->setEnabled(false);
		ui_->menuExport_GPS->setEnabled(false);
		ui_->actionPoses_KML->setEnabled(false);
		ui_->actionEdit_optimized_2D_map->setEnabled(false);
		ui_->actionExport_saved_2D_map->setEnabled(false);
		ui_->actionImport_2D_map->setEnabled(false);
		ui_->actionRegenerate_optimized_2D_map->setEnabled(false);
		ui_->actionView_optimized_mesh->setEnabled(false);
		ui_->actionExport_optimized_mesh->setEnabled(false);
		ui_->actionUpdate_optimized_mesh->setEnabled(false);
		ui_->checkBox_showOptimized->setEnabled(false);
		ui_->toolBox_statistics->clear();
		databaseFileName_.clear();
		ui_->checkBox_alignPosesWithGroundTruth->setVisible(false);
		ui_->checkBox_alignScansCloudsWithGroundTruth->setVisible(false);
		ui_->doubleSpinBox_optimizationScale->setVisible(false);
		ui_->label_scale_title->setVisible(false);
		ui_->label_rmse->setVisible(false);
		ui_->label_rmse_title->setVisible(false);
		ui_->checkBox_ignoreIntermediateNodes->setVisible(false);
		ui_->label_ignoreINtermediateNdoes->setVisible(false);
		ui_->label_alignPosesWithGroundTruth->setVisible(false);
		ui_->label_alignScansCloudsWithGroundTruth->setVisible(false);
		ui_->label_optimizeFrom->setText(tr("Root"));
		ui_->textEdit_info->clear();

		ui_->pushButton_refine->setEnabled(false);
		ui_->pushButton_add->setEnabled(false);
		ui_->pushButton_reset->setEnabled(false);
		ui_->pushButton_reject->setEnabled(false);

		ui_->horizontalSlider_loops->setEnabled(false);
		ui_->horizontalSlider_loops->setMaximum(0);
		ui_->horizontalSlider_iterations->setEnabled(false);
		ui_->horizontalSlider_iterations->setMaximum(0);
		ui_->horizontalSlider_neighbors->setEnabled(false);
		ui_->horizontalSlider_neighbors->setMaximum(0);
		ui_->label_constraint->clear();
		ui_->label_constraint_opt->clear();
		ui_->label_variance->clear();
		ui_->lineEdit_covariance->clear();

		ui_->horizontalSlider_A->setEnabled(false);
		ui_->horizontalSlider_A->setMaximum(0);
		ui_->horizontalSlider_B->setEnabled(false);
		ui_->horizontalSlider_B->setMaximum(0);
		ui_->label_idA->setText("NaN");
		ui_->label_idB->setText("NaN");
		sliderAValueChanged(0);
		sliderBValueChanged(0);

		constraintsViewer_->clear();
		constraintsViewer_->update();

		cloudViewer_->clear();
		cloudViewer_->update();

		occupancyGridViewer_->clear();
		occupancyGridViewer_->update();

		ui_->graphViewer->clearAll();
		ui_->label_loopClosures->clear();
		ui_->label_timeOptimization->clear();
		ui_->label_pathLength->clear();
		ui_->label_poses->clear();
		ui_->label_rmse->clear();
		ui_->spinBox_optimizationsFrom->setEnabled(false);

		ui_->graphicsView_A->clear();
		ui_->graphicsView_B->clear();

		ui_->graphicsView_stereo->clear();
		stereoViewer_->clear();
		stereoViewer_->update();

		ui_->toolBox_statistics->clear();

		useLastOptimizedGraphAsGuess_ = false;
		lastOptimizedGraph_.clear();
	}

	ui_->actionClose_database->setEnabled(dbDriver_ != 0);
	ui_->actionOpen_database->setEnabled(dbDriver_ == 0);

	return dbDriver_ == 0;
}


void DatabaseViewer::recoverDatabase()
{
	QString path = QFileDialog::getOpenFileName(this, tr("Select file"), pathDatabase_, tr("Databases (*.db)"));
	if(!path.isEmpty())
	{
		if(path.compare(pathDatabase_+QDir::separator()+databaseFileName_.c_str()) == 0)
		{
			QMessageBox::information(this, "Database recovery", tr("The selected database is already opened, close it first."));
			return;
		}
		std::string errorMsg;
		rtabmap::ProgressDialog * progressDialog = new rtabmap::ProgressDialog(this);
		progressDialog->setAttribute(Qt::WA_DeleteOnClose);
		progressDialog->setMaximumSteps(100);
		progressDialog->show();
		progressDialog->setCancelButtonVisible(true);
		RecoveryState state(progressDialog);
		if(databaseRecovery(path.toStdString(), false, &errorMsg, &state))
		{
			QMessageBox::information(this, "Database recovery", tr("Database \"%1\" recovered! Try opening it again.").arg(path));
		}
		else
		{
			QMessageBox::warning(this, "Database recovery", tr("Database recovery failed: \"%1\".").arg(errorMsg.c_str()));
		}
		progressDialog->setValue(progressDialog->maximumSteps());
	}
}

void DatabaseViewer::closeEvent(QCloseEvent* event)
{
	//write settings before quit?
	bool save = false;
	if(this->isWindowModified())
	{
		QMessageBox::Button b=QMessageBox::question(this,
				tr("Database Viewer"),
				tr("There are unsaved changed settings. Save them?"),
				QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
		if(b == QMessageBox::Save)
		{
			save = true;
		}
		else if(b != QMessageBox::Discard)
		{
			event->ignore();
			return;
		}
	}

	if(save)
	{
		writeSettings();
	}

	event->accept();

	if(!closeDatabase())
	{
		event->ignore();
	}

	if(event->isAccepted())
	{
		ui_->toolBox_statistics->closeFigures();
		if(dbDriver_)
		{
			delete dbDriver_;
			dbDriver_ = 0;
		}
	}
}

void DatabaseViewer::showEvent(QShowEvent* anEvent)
{
	this->setWindowModified(false);

	if((ui_->graphViewer->isVisible() || ui_->dockWidget_occupancyGridView->isVisible()) && graphes_.size() && localMaps_.size()==0)
	{
		sliderIterationsValueChanged((int)graphes_.size()-1);
	}
}

void DatabaseViewer::moveEvent(QMoveEvent* anEvent)
{
	if(this->isVisible())
	{
		// HACK, there is a move event when the window is shown the first time.
		if(!firstCall_)
		{
			this->configModified();
		}
		firstCall_ = false;
	}
}

void DatabaseViewer::resizeEvent(QResizeEvent* anEvent)
{
	if(this->isVisible())
	{
		this->configModified();
	}
}

void DatabaseViewer::keyPressEvent(QKeyEvent *event)
{
	//catch ctrl-s to save settings
	if((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_S)
	{
		this->writeSettings();
	}
}

bool DatabaseViewer::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::Resize && qobject_cast<QDockWidget*>(obj))
	{
		this->setWindowModified(true);
	}
	return QWidget::eventFilter(obj, event);
}


void DatabaseViewer::exportDatabase()
{
	if(!dbDriver_ || ids_.size() == 0)
	{
		return;
	}

	rtabmap::ExportDialog dialog;

	if(dialog.exec())
	{
		if(!dialog.outputPath().isEmpty())
		{
			int framesIgnored = dialog.framesIgnored();
			double frameRate = dialog.targetFramerate();
			int sessionExported = dialog.sessionExported();
			QString path = dialog.outputPath();
			rtabmap::DataRecorder recorder;
			QList<int> ids;

			double previousStamp = 0;
			std::vector<double> delays(ids_.size());
			int oi=0;
			std::map<int, Transform> poses;
			std::map<int, double> stamps;
			std::map<int, Transform> groundTruths;
			std::map<int, GPS> gpsValues;
			std::map<int, EnvSensors> sensorsValues;
			for(int i=0; i<ids_.size(); i+=1+framesIgnored)
			{
				Transform odomPose, groundTruth;
				int weight = -1;
				int mapId = -1;
				std::string label;
				double stamp = 0;
				std::vector<float> velocity;
				GPS gps;
				EnvSensors sensors;
				if(dbDriver_->getNodeInfo(ids_[i], odomPose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors))
				{
					if(frameRate == 0 ||
					   previousStamp == 0 ||
					   stamp == 0 ||
					   stamp - previousStamp >= 1.0/frameRate)
					{
						if(sessionExported < 0 || sessionExported == mapId)
						{
							ids.push_back(ids_[i]);

							if(previousStamp && stamp)
							{
								delays[oi++] = stamp - previousStamp;
							}
							previousStamp = stamp;

							poses.insert(std::make_pair(ids_[i], odomPose));
							stamps.insert(std::make_pair(ids_[i], stamp));
							groundTruths.insert(std::make_pair(ids_[i], groundTruth));
							if(gps.stamp() > 0.0)
							{
								gpsValues.insert(std::make_pair(ids_[i], gps));
							}
							if(sensors.size())
							{
								sensorsValues.insert(std::make_pair(ids_[i], sensors));
							}
						}
					}
					if(sessionExported >= 0 && mapId > sessionExported)
					{
						break;
					}
				}
			}
			delays.resize(oi);

			if(recorder.init(path, false))
			{
				rtabmap::ProgressDialog * progressDialog = new rtabmap::ProgressDialog(this);
				progressDialog->setAttribute(Qt::WA_DeleteOnClose);
				progressDialog->setMaximumSteps(ids.size());
				progressDialog->show();
				progressDialog->setCancelButtonVisible(true);
				UINFO("Decompress: rgb=%d depth=%d scan=%d userData=%d",
						dialog.isRgbExported()?1:0,
						dialog.isDepthExported()?1:0,
						dialog.isDepth2dExported()?1:0,
						dialog.isUserDataExported()?1:0);

				for(int i=0; i<ids.size() && !progressDialog->isCanceled(); ++i)
				{
					int id = ids.at(i);

					SensorData data;
					dbDriver_->getNodeData(id, data);
					cv::Mat depth, rgb, userData;
					LaserScan scan;
					data.uncompressDataConst(
							!dialog.isRgbExported()?0:&rgb,
							!dialog.isDepthExported()?0:&depth,
							!dialog.isDepth2dExported()?0:&scan,
							!dialog.isUserDataExported()?0:&userData);
					cv::Mat covariance = cv::Mat::eye(6,6,CV_64FC1);
					if(dialog.isOdomExported())
					{
						std::multimap<int, Link> links;
						dbDriver_->loadLinks(id, links, Link::kNeighbor);
						if(links.size() && links.begin()->first < id)
						{
							covariance = links.begin()->second.infMatrix().inv();
						}
					}

					rtabmap::SensorData sensorData;
					if(data.cameraModels().size())
					{
						sensorData = rtabmap::SensorData(
							scan,
							rgb,
							depth,
							data.cameraModels(),
							id,
							stamps.at(id),
							userData);
					}
					else
					{
						sensorData = rtabmap::SensorData(
							scan,
							rgb,
							depth,
							data.stereoCameraModel(),
							id,
							stamps.at(id),
							userData);
					}
					if(groundTruths.find(id)!=groundTruths.end())
					{
						sensorData.setGroundTruth(groundTruths.at(id));
					}
					if(gpsValues.find(id)!=gpsValues.end())
					{
						sensorData.setGPS(gpsValues.at(id));
					}
					if(sensorsValues.find(id)!=sensorsValues.end())
					{
						sensorData.setEnvSensors(sensorsValues.at(id));
					}

					recorder.addData(sensorData, dialog.isOdomExported()?poses.at(id):Transform(), covariance);

					progressDialog->appendText(tr("Exported node %1").arg(id));
					progressDialog->incrementStep();
					QApplication::processEvents();
				}
				progressDialog->setValue(progressDialog->maximumSteps());
				if(delays.size())
				{
					progressDialog->appendText(tr("Average frame rate=%1 Hz (Min=%2, Max=%3)")
							.arg(1.0/uMean(delays)).arg(1.0/uMax(delays)).arg(1.0/uMin(delays)));
				}
				progressDialog->appendText(tr("Export finished to \"%1\"!").arg(path));
			}
			else
			{
				UERROR("DataRecorder init failed?!");
			}
		}
		else
		{
			QMessageBox::warning(this, tr("Cannot export database"), tr("An output path must be set!"));
		}
	}
}

void DatabaseViewer::extractImages()
{
	if(!dbDriver_ || ids_.size() == 0)
	{
		return;
	}

	QStringList formats;
	formats.push_back("jpg");
	formats.push_back("png");
	bool ok;
	QString ext = QInputDialog::getItem(this, tr("Which RGB format?"), tr("Format:"), formats, 0, false, &ok);
	if(!ok)
	{
		return;
	}

	QString path = QFileDialog::getExistingDirectory(this, tr("Select directory where to save images..."), QDir::homePath());
	if(!path.isEmpty())
	{
		if(ids_.size())
		{
			int id = ids_.at(0);
			SensorData data;
			dbDriver_->getNodeData(id, data);
			data.uncompressData();
			if(!data.imageRaw().empty() && !data.rightRaw().empty())
			{
				QDir dir;
				dir.mkdir(QString("%1/left").arg(path));
				dir.mkdir(QString("%1/right").arg(path));
				if(databaseFileName_.empty())
				{
					UERROR("Cannot save calibration file, database name is empty!");
				}
				else if(data.stereoCameraModel().isValidForProjection())
				{
					std::string cameraName = uSplit(databaseFileName_, '.').front();
					StereoCameraModel model(
							cameraName,
							data.imageRaw().size(),
							data.stereoCameraModel().left().K(),
							data.stereoCameraModel().left().D(),
							data.stereoCameraModel().left().R(),
							data.stereoCameraModel().left().P(),
							data.rightRaw().size(),
							data.stereoCameraModel().right().K(),
							data.stereoCameraModel().right().D(),
							data.stereoCameraModel().right().R(),
							data.stereoCameraModel().right().P(),
							data.stereoCameraModel().R(),
							data.stereoCameraModel().T(),
							data.stereoCameraModel().E(),
							data.stereoCameraModel().F(),
							data.stereoCameraModel().left().localTransform());
					if(model.save(path.toStdString()))
					{
						UINFO("Saved stereo calibration \"%s\"", (path.toStdString()+"/"+cameraName).c_str());
					}
					else
					{
						UERROR("Failed saving calibration \"%s\"", (path.toStdString()+"/"+cameraName).c_str());
					}
				}
			}
			else if(!data.imageRaw().empty())
			{
				if(!data.depthRaw().empty())
				{
					QDir dir;
					dir.mkdir(QString("%1/rgb").arg(path));
					dir.mkdir(QString("%1/depth").arg(path));
				}

				if(databaseFileName_.empty())
				{
					UERROR("Cannot save calibration file, database name is empty!");
				}
				else if(data.cameraModels().size() > 1)
				{
					UERROR("Only one camera calibration can be saved at this time (%d detected)", (int)data.cameraModels().size());
				}
				else if(data.cameraModels().size() == 1 && data.cameraModels().front().isValidForProjection())
				{
					std::string cameraName = uSplit(databaseFileName_, '.').front();
					CameraModel model(cameraName,
							data.imageRaw().size(),
							data.cameraModels().front().K(),
							data.cameraModels().front().D(),
							data.cameraModels().front().R(),
							data.cameraModels().front().P(),
							data.cameraModels().front().localTransform());
					if(model.save(path.toStdString()))
					{
						UINFO("Saved calibration \"%s\"", (path.toStdString()+"/"+cameraName).c_str());
					}
					else
					{
						UERROR("Failed saving calibration \"%s\"", (path.toStdString()+"/"+cameraName).c_str());
					}
				}
			}
		}

		int imagesExported = 0;
		for(int i=0; i<ids_.size(); ++i)
		{
			int id = ids_.at(i);
			SensorData data;
			dbDriver_->getNodeData(id, data);
			data.uncompressData();
			if(!data.imageRaw().empty() && !data.rightRaw().empty())
			{
				cv::imwrite(QString("%1/left/%2.%3").arg(path).arg(id).arg(ext).toStdString(), data.imageRaw());
				cv::imwrite(QString("%1/right/%2.%3").arg(path).arg(id).arg(ext).toStdString(), data.rightRaw());
				UINFO(QString("Saved left/%1.%2 and right/%1.%2").arg(id).arg(ext).toStdString().c_str());
				++imagesExported;
			}
			else if(!data.imageRaw().empty() && !data.depthRaw().empty())
			{
				cv::imwrite(QString("%1/rgb/%2.%3").arg(path).arg(id).arg(ext).toStdString(), data.imageRaw());
				cv::imwrite(QString("%1/depth/%2.png").arg(path).arg(id).toStdString(), data.depthRaw().type()==CV_32FC1?util2d::cvtDepthFromFloat(data.depthRaw()):data.depthRaw());
				UINFO(QString("Saved rgb/%1.%2 and depth/%1.png").arg(id).arg(ext).toStdString().c_str());
				++imagesExported;
			}
			else if(!data.imageRaw().empty())
			{
				cv::imwrite(QString("%1/%2.%3").arg(path).arg(id).arg(ext).toStdString(), data.imageRaw());
				UINFO(QString("Saved %1.%2").arg(id).arg(ext).toStdString().c_str());
				++imagesExported;
			}
		}
		QMessageBox::information(this, tr("Exporting"), tr("%1 images exported!").arg(imagesExported));
	}
}

void DatabaseViewer::updateIds()
{
	if(!dbDriver_)
	{
		return;
	}

	rtabmap::ProgressDialog * progressDialog = new rtabmap::ProgressDialog(this);
	progressDialog->setAttribute(Qt::WA_DeleteOnClose);
	int progressSteps = 5;
	if(ui_->graphViewer->isVisible() || ui_->dockWidget_occupancyGridView->isVisible())
	{
		++progressSteps;
	}
	if(ui_->textEdit_info->isVisible())
	{
		++progressSteps;
	}
	if(ui_->toolBox_statistics->isVisible())
	{
		++progressSteps;
	}
	progressDialog->setMaximumSteps(progressSteps);
	progressDialog->show();
	progressDialog->setCancelButtonVisible(false);

	progressDialog->appendText(tr("Loading all ids..."));
	QApplication::processEvents();
	uSleep(100);
	QApplication::processEvents();

	UINFO("Loading all IDs...");
	std::set<int> ids;
	dbDriver_->getAllNodeIds(ids);
	ids_ = QList<int>::fromStdList(std::list<int>(ids.begin(), ids.end()));
	lastWmIds_.clear();
	dbDriver_->getLastNodeIds(lastWmIds_);
	idToIndex_.clear();
	mapIds_.clear();
	weights_.clear();
	wmStates_.clear();
	odomPoses_.clear();
	dbOptimizedPoses_.clear();
	groundTruthPoses_.clear();
	gpsPoses_.clear();
	gpsValues_.clear();
	lastSliderIndexBrowsed_ = 0;
	ui_->checkBox_wmState->setVisible(false);
	ui_->checkBox_alignPosesWithGroundTruth->setVisible(false);
	ui_->checkBox_alignScansCloudsWithGroundTruth->setVisible(false);
	ui_->doubleSpinBox_optimizationScale->setVisible(false);
	ui_->label_scale_title->setVisible(false);
	ui_->label_rmse->setVisible(false);
	ui_->label_rmse_title->setVisible(false);
	ui_->checkBox_ignoreIntermediateNodes->setVisible(false);
	ui_->label_ignoreINtermediateNdoes->setVisible(false);
	ui_->label_alignPosesWithGroundTruth->setVisible(false);
	ui_->label_alignScansCloudsWithGroundTruth->setVisible(false);
	ui_->menuEdit->setEnabled(true);
	ui_->actionGenerate_3D_map_pcd->setEnabled(true);
	ui_->actionExport->setEnabled(true);
	ui_->actionExtract_images->setEnabled(true);
	ui_->menuExport_poses->setEnabled(false);
	ui_->menuExport_GPS->setEnabled(false);
	ui_->actionPoses_KML->setEnabled(false);
	ui_->actionEdit_optimized_2D_map->setEnabled(false);
	ui_->actionExport_saved_2D_map->setEnabled(false);
	ui_->actionImport_2D_map->setEnabled(false);
	ui_->actionRegenerate_optimized_2D_map->setEnabled(false);
	ui_->actionView_optimized_mesh->setEnabled(false);
	ui_->actionExport_optimized_mesh->setEnabled(false);
	ui_->actionUpdate_optimized_mesh->setEnabled(uStrNumCmp(dbDriver_->getDatabaseVersion(), "0.13.0") >= 0);
	links_.clear();
	linksAdded_.clear();
	linksRefined_.clear();
	linksRemoved_.clear();
	ui_->toolBox_statistics->clear();
	ui_->label_optimizeFrom->setText(tr("Root"));

	progressDialog->appendText(tr("%1 ids loaded!").arg(ids.size()));
	progressDialog->incrementStep();
	progressDialog->appendText(tr("Loading all links..."));
	QApplication::processEvents();
	uSleep(100);
	QApplication::processEvents();

	std::multimap<int, Link> unilinks;
	dbDriver_->getAllLinks(unilinks, true, true);
	UDEBUG("%d total links loaded", (int)unilinks.size());
	// add both direction links
	std::multimap<int, Link> links;
	for(std::multimap<int, Link>::iterator iter=unilinks.begin(); iter!=unilinks.end(); ++iter)
	{
		links.insert(*iter);
		if(graph::findLink(unilinks, iter->second.to(), iter->second.from(), false) == unilinks.end())
		{
			links.insert(std::make_pair(iter->second.to(), iter->second.inverse()));
		}
	}

	progressDialog->appendText(tr("%1 links loaded!").arg(unilinks.size()));
	progressDialog->incrementStep();
	progressDialog->appendText("Loading Working Memory state...");
	QApplication::processEvents();
	uSleep(100);
	QApplication::processEvents();

	infoTotalOdom_ = 0.0;
	Transform previousPose;
	infoSessions_ = ids_.size()?1:0;
	infoTotalTime_ = 0.0;
	double previousStamp = 0.0;
	infoReducedGraph_ = false;
	std::map<int, std::vector<int> > wmStates = dbDriver_->getAllStatisticsWmStates();

	progressDialog->appendText("Loading Working Memory state... done!");
	progressDialog->incrementStep();
	progressDialog->appendText("Loading info for all nodes...");
	QApplication::processEvents();
	uSleep(100);
	QApplication::processEvents();

	for(int i=0; i<ids_.size(); ++i)
	{
		idToIndex_.insert(ids_[i], i);

		Transform p, g;
		int w;
		std::string l;
		double s;
		int mapId;
		std::vector<float> v;
		GPS gps;
		EnvSensors sensors;
		dbDriver_->getNodeInfo(ids_[i], p, mapId, w, l, s, g, v, gps, sensors);
		mapIds_.insert(std::make_pair(ids_[i], mapId));
		weights_.insert(std::make_pair(ids_[i], w));
		if(wmStates.find(ids_[i]) != wmStates.end())
		{
			wmStates_.insert(std::make_pair(ids_[i], wmStates.at(ids_[i])));
			ui_->checkBox_wmState->setVisible(true);
		}
		if(w < 0)
		{
			ui_->checkBox_ignoreIntermediateNodes->setVisible(true);
			ui_->label_ignoreINtermediateNdoes->setVisible(true);
		}
		if(i>0)
		{
			if(mapIds_.at(ids_[i-1]) == mapId)
			{
				if(!p.isNull() && !previousPose.isNull())
				{
					infoTotalOdom_ += p.getDistance(previousPose);
				}

				if(previousStamp > 0.0 && s > 0.0)
				{
					infoTotalTime_ += s-previousStamp;
				}
			}
			else
			{
				++infoSessions_;
			}
		}
		previousStamp=s;
		previousPose=p;

		//links
		bool addPose = false;
		for(std::multimap<int, Link>::iterator jter=links.find(ids_[i]); jter!=links.end() && jter->first == ids_[i]; ++jter)
		{
			if(jter->second.type() == Link::kNeighborMerged)
			{
				infoReducedGraph_ = true;
			}

			std::multimap<int, Link>::iterator invertedLinkIter = graph::findLink(links, jter->second.to(), jter->second.from(), false);
			if(	jter->second.isValid() && // null transform means a rehearsed location
				ids.find(jter->second.from()) != ids.end() &&
				(ids.find(jter->second.to()) != ids.end() || jter->second.to()<0) && // to add landmark links
				graph::findLink(links_, jter->second.from(), jter->second.to()) == links_.end() &&
				invertedLinkIter != links.end())
			{
				// check if user_data is set in opposite direction
				if(jter->second.userDataCompressed().cols == 0 &&
				   invertedLinkIter->second.userDataCompressed().cols != 0)
				{
					links_.insert(std::make_pair(invertedLinkIter->second.from(), invertedLinkIter->second));
				}
				else
				{
					links_.insert(std::make_pair(ids_[i], jter->second));
				}
				addPose = true;
			}
			else if(graph::findLink(links_, jter->second.from(), jter->second.to()) != links_.end())
			{
				addPose = true;
			}
		}
		if(addPose)
		{
			odomPoses_.insert(std::make_pair(ids_[i], p));
			if(!g.isNull())
			{
				groundTruthPoses_.insert(std::make_pair(ids_[i], g));
			}
			if(gps.stamp() > 0.0)
			{
				gpsValues_.insert(std::make_pair(ids_[i], gps));

				cv::Point3f p(0.0f,0.0f,0.0f);
				if(!gpsPoses_.empty())
				{
					GeodeticCoords coords = gps.toGeodeticCoords();
					GPS originGPS = gpsValues_.begin()->second;
					p = coords.toENU_WGS84(originGPS.toGeodeticCoords());
				}
				Transform pose(p.x, p.y, p.z, 0.0f, 0.0f, (float)((-(gps.bearing()-90))*M_PI/180.0));
				gpsPoses_.insert(std::make_pair(ids_[i], pose));
			}
		}
	}

	progressDialog->appendText("Loading info for all nodes... done!");
	progressDialog->incrementStep();
	progressDialog->appendText("Loading optimized poses and maps...");
	QApplication::processEvents();
	uSleep(100);
	QApplication::processEvents();

	dbOptimizedPoses_ = dbDriver_->loadOptimizedPoses();

	if(!groundTruthPoses_.empty() || !gpsPoses_.empty())
	{
		ui_->checkBox_alignPosesWithGroundTruth->setVisible(true);
		ui_->doubleSpinBox_optimizationScale->setVisible(true);
		ui_->label_scale_title->setVisible(true);
		ui_->label_rmse->setVisible(true);
		ui_->label_rmse_title->setVisible(true);
		ui_->label_alignPosesWithGroundTruth->setVisible(true);

		if(!groundTruthPoses_.empty())
		{
			ui_->label_alignPosesWithGroundTruth->setText(tr("Align poses with ground truth"));
			ui_->checkBox_alignScansCloudsWithGroundTruth->setVisible(true);
			ui_->label_alignScansCloudsWithGroundTruth->setVisible(true);
		}
		else
		{
			ui_->label_alignPosesWithGroundTruth->setText(tr("Align poses with GPS"));
		}
	}
	if(!gpsValues_.empty())
	{
		ui_->menuExport_GPS->setEnabled(true);
		ui_->actionPoses_KML->setEnabled(groundTruthPoses_.empty());
	}

	float xMin, yMin, cellSize;
	bool hasMap = !dbDriver_->load2DMap(xMin, yMin, cellSize).empty();
	ui_->actionEdit_optimized_2D_map->setEnabled(hasMap);
	ui_->actionExport_saved_2D_map->setEnabled(hasMap);
	ui_->actionImport_2D_map->setEnabled(hasMap);
	ui_->actionRegenerate_optimized_2D_map->setEnabled(uStrNumCmp(dbDriver_->getDatabaseVersion(), "0.17.0") >= 0);

	if(!dbDriver_->loadOptimizedMesh().empty())
	{
		ui_->actionView_optimized_mesh->setEnabled(true);
		ui_->actionExport_optimized_mesh->setEnabled(true);
	}

	UINFO("Loaded %d ids, %d poses and %d links", (int)ids_.size(), (int)odomPoses_.size(), (int)links_.size());

	progressDialog->appendText("Loading optimized poses and maps... done!");
	progressDialog->incrementStep();
	QApplication::processEvents();
	uSleep(100);
	QApplication::processEvents();

	if(ids_.size() && ui_->toolBox_statistics->isVisible())
	{
		progressDialog->appendText("Loading statistics...");
		QApplication::processEvents();
		uSleep(100);
		QApplication::processEvents();

		UINFO("Update statistics...");
		updateStatistics();

		progressDialog->appendText("Loading statistics... done!");
		progressDialog->incrementStep();
		QApplication::processEvents();
		uSleep(100);
		QApplication::processEvents();
	}


	ui_->textEdit_info->clear();
	if(ui_->textEdit_info->isVisible())
	{
		progressDialog->appendText("Update database info...");
		QApplication::processEvents();
		uSleep(100);
		QApplication::processEvents();

		updateInfo();

		progressDialog->appendText("Update database info... done!");
		progressDialog->incrementStep();
		QApplication::processEvents();
		uSleep(100);
		QApplication::processEvents();
	}

	if(ids.size())
	{
		if(odomPoses_.size())
		{
			bool nullPoses = odomPoses_.begin()->second.isNull();
			for(std::map<int,Transform>::iterator iter=odomPoses_.begin(); iter!=odomPoses_.end(); ++iter)
			{
				if((!iter->second.isNull() && nullPoses) ||
					(iter->second.isNull() && !nullPoses))
				{
					if(iter->second.isNull())
					{
						UWARN("Pose %d is null!", iter->first);
					}
					UWARN("Mixed valid and null poses! Ignoring graph...");
					odomPoses_.clear();
					links_.clear();
					break;
				}
			}
			if(nullPoses)
			{
				odomPoses_.clear();
				links_.clear();
			}

			if(odomPoses_.size())
			{
				ui_->spinBox_optimizationsFrom->setRange(odomPoses_.begin()->first, odomPoses_.rbegin()->first);
				ui_->spinBox_optimizationsFrom->setValue(odomPoses_.begin()->first);
				ui_->label_optimizeFrom->setText(tr("Root [%1, %2]").arg(odomPoses_.begin()->first).arg(odomPoses_.rbegin()->first));
			}
		}
	}

	ui_->menuExport_poses->setEnabled(false);
	graphes_.clear();
	graphLinks_.clear();
	neighborLinks_.clear();
	loopLinks_.clear();
	for(std::multimap<int, rtabmap::Link>::iterator iter = links_.begin(); iter!=links_.end(); ++iter)
	{
		if(!iter->second.transform().isNull())
		{
			if(iter->second.type() == rtabmap::Link::kNeighbor ||
			   iter->second.type() == rtabmap::Link::kNeighborMerged)
			{
				neighborLinks_.append(iter->second);
			}
			else if(iter->second.from()!=iter->second.to())
			{
				loopLinks_.append(iter->second);
			}
		}
		else
		{
			UERROR("Transform null for link from %d to %d", iter->first, iter->second.to());
		}
	}

	if(ids_.size())
	{
		ui_->horizontalSlider_A->setMinimum(0);
		ui_->horizontalSlider_B->setMinimum(0);
		ui_->horizontalSlider_A->setMaximum(ids_.size()-1);
		ui_->horizontalSlider_B->setMaximum(ids_.size()-1);
		ui_->horizontalSlider_A->setEnabled(true);
		ui_->horizontalSlider_B->setEnabled(true);
		ui_->horizontalSlider_A->setSliderPosition(0);
		ui_->horizontalSlider_B->setSliderPosition(0);
		sliderAValueChanged(0);
		sliderBValueChanged(0);
	}
	else
	{
		ui_->horizontalSlider_A->setEnabled(false);
		ui_->horizontalSlider_B->setEnabled(false);
		ui_->label_idA->setText("NaN");
		ui_->label_idB->setText("NaN");
	}

	if(neighborLinks_.size())
	{
		ui_->horizontalSlider_neighbors->setMinimum(0);
		ui_->horizontalSlider_neighbors->setMaximum(neighborLinks_.size()-1);
		ui_->horizontalSlider_neighbors->setEnabled(true);
		ui_->horizontalSlider_neighbors->setSliderPosition(0);
	}
	else
	{
		ui_->horizontalSlider_neighbors->setEnabled(false);
	}

	if(ids_.size())
	{
		updateLoopClosuresSlider();
		if(ui_->graphViewer->isVisible() || ui_->dockWidget_occupancyGridView->isVisible())
		{
			progressDialog->appendText("Updating Graph View...");
			QApplication::processEvents();
			uSleep(100);
			QApplication::processEvents();

			updateGraphView();

			progressDialog->appendText("Updating Graph View... done!");
			progressDialog->incrementStep();
			QApplication::processEvents();
			uSleep(100);
			QApplication::processEvents();
		}
	}
	progressDialog->setValue(progressDialog->maximumSteps());
}

void DatabaseViewer::updateInfo()
{
	UINFO("Update database info...");
	if(dbDriver_)
	{
		if(ui_->textEdit_info->toPlainText().isEmpty())
		{
			ui_->textEdit_info->append(tr("Path:\t\t%1").arg(dbDriver_->getUrl().c_str()));
			ui_->textEdit_info->append(tr("Version:\t\t%1").arg(dbDriver_->getDatabaseVersion().c_str()));
			ui_->textEdit_info->append(tr("Sessions:\t\t%1").arg(infoSessions_));
			if(infoReducedGraph_)
			{
				ui_->textEdit_info->append(tr("Total odometry length:\t%1 m (approx. as graph has been reduced)").arg(infoTotalOdom_));
			}
			else
			{
				ui_->textEdit_info->append(tr("Total odometry length:\t%1 m").arg(infoTotalOdom_));
			}
			ui_->textEdit_info->append(tr("Total time:\t\t%1").arg(QDateTime::fromMSecsSinceEpoch(infoTotalTime_*1000).toUTC().toString("hh:mm:ss.zzz")));
			ui_->textEdit_info->append(tr("LTM:\t\t%1 nodes and %2 words").arg(ids_.size()).arg(dbDriver_->getTotalDictionarySize()));
			ui_->textEdit_info->append(tr("WM:\t\t%1 nodes and %2 words").arg(dbDriver_->getLastNodesSize()).arg(dbDriver_->getLastDictionarySize()));
			ui_->textEdit_info->append(tr("Global graph:\t%1 poses and %2 links").arg(odomPoses_.size()).arg(links_.size()));
			ui_->textEdit_info->append(tr("Ground truth:\t%1 poses").arg(groundTruthPoses_.size()));
			ui_->textEdit_info->append(tr("GPS:\t%1 poses").arg(gpsValues_.size()));
			ui_->textEdit_info->append("");
			long total = 0;
			long dbSize = UFile::length(dbDriver_->getUrl());
			long mem = dbSize;
			ui_->textEdit_info->append(tr("Database size:\t%1 %2").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes"));
			mem = dbDriver_->getNodesMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Nodes size:\t\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getLinksMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Links size:\t\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getImagesMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("RGB Images size:\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getDepthImagesMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Depth Images size:\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getCalibrationsMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Calibrations size:\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getGridsMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Grids size:\t\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getLaserScansMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Scans size:\t\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getUserDataMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("User data size:\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getWordsMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Dictionary size:\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getFeaturesMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Features size:\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbDriver_->getStatisticsMemoryUsed();
			total+=mem;
			ui_->textEdit_info->append(tr("Statistics size:\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			mem = dbSize - total;
			ui_->textEdit_info->append(tr("Other (indexing):\t%1 %2\t%3%").arg(mem>1000000?mem/1000000:mem>1000?mem/1000:mem).arg(mem>1000000?"MB":mem>1000?"KB":"Bytes").arg(dbSize>0?QString::number(double(mem)/double(dbSize)*100.0, 'f', 2 ):"0"));
			ui_->textEdit_info->append("");
			std::set<int> idsWithoutBad;
			dbDriver_->getAllNodeIds(idsWithoutBad, false, true);
			int infoBadcountInLTM = 0;
			int infoBadCountInGraph = 0;
			for(int i=0; i<ids_.size(); ++i)
			{
				if(idsWithoutBad.find(ids_[i]) == idsWithoutBad.end())
				{
					++infoBadcountInLTM;
					if(odomPoses_.find(ids_[i]) != odomPoses_.end())
					{
						++infoBadCountInGraph;
					}
				}
			}
			ui_->textEdit_info->append(tr("%1 bad signatures in LTM").arg(infoBadcountInLTM));
			ui_->textEdit_info->append(tr("%1 bad signatures in the global graph").arg(infoBadCountInGraph));
			ui_->textEdit_info->append("");
			ParametersMap parameters = dbDriver_->getLastParameters();
			QFontMetrics metrics(ui_->textEdit_info->font());
			int tabW = ui_->textEdit_info->tabStopWidth();
			for(ParametersMap::iterator iter=parameters.begin(); iter!=parameters.end(); ++iter)
			{
				int strW = metrics.width(QString(iter->first.c_str()) + "=");
				ui_->textEdit_info->append(tr("%1=%2%3")
						.arg(iter->first.c_str())
						.arg(strW < tabW?"\t\t\t\t":strW < tabW*2?"\t\t\t":strW < tabW*3?"\t\t":"\t")
						.arg(iter->second.c_str()));
			}

			// move back the cursor at the beginning
			ui_->textEdit_info->moveCursor(QTextCursor::Start) ;
			ui_->textEdit_info->ensureCursorVisible() ;
		}
	}
	else
	{
		ui_->textEdit_info->clear();
	}
}

void DatabaseViewer::updateStatistics()
{
	UDEBUG("");
	if(dbDriver_)
	{
		ui_->toolBox_statistics->clear();
		double firstStamp = 0.0;
		std::map<int, std::pair<std::map<std::string, float>, double> > allStats = dbDriver_->getAllStatistics();

		std::map<std::string, std::pair<std::vector<float>, std::vector<float> > > allData;
		std::map<std::string, int > allDataOi;

		for(int i=0; i<ids_.size(); ++i)
		{
			double stamp=0.0;
			std::map<std::string, float> statistics;
			if(allStats.find(ids_[i]) != allStats.end())
			{
				statistics = allStats.at(ids_[i]).first;
				stamp = allStats.at(ids_[i]).second;
			}
			if(firstStamp==0.0)
			{
				firstStamp = stamp;
			}
			for(std::map<std::string, float>::iterator iter=statistics.begin(); iter!=statistics.end(); ++iter)
			{
				if(allData.find(iter->first) == allData.end())
				{
					//initialize data vectors
					allData.insert(std::make_pair(iter->first, std::make_pair(std::vector<float>(ids_.size(), 0.0f), std::vector<float>(ids_.size(), 0.0f) )));
					allDataOi.insert(std::make_pair(iter->first, 0));
				}

				int & oi = allDataOi.at(iter->first);
				allData.at(iter->first).first[oi] = ui_->checkBox_timeStats->isChecked()?float(stamp-firstStamp):ids_[i];
				allData.at(iter->first).second[oi] = iter->second;
				++oi;
			}
		}

		for(std::map<std::string, std::pair<std::vector<float>, std::vector<float> > >::iterator iter=allData.begin(); iter!=allData.end(); ++iter)
		{
			int oi = allDataOi.at(iter->first);
			iter->second.first.resize(oi);
			iter->second.second.resize(oi);
			ui_->toolBox_statistics->updateStat(iter->first.c_str(), iter->second.first, iter->second.second, true);
		}
	}
	UDEBUG("");
}

void DatabaseViewer::selectObstacleColor()
{
	QColor c = QColorDialog::getColor(ui_->lineEdit_obstacleColor->text(), this);
	if(c.isValid())
	{
		ui_->lineEdit_obstacleColor->setText(c.name());
	}
}
void DatabaseViewer::selectGroundColor()
{
	QColor c = QColorDialog::getColor(ui_->lineEdit_groundColor->text(), this);
	if(c.isValid())
	{
		ui_->lineEdit_groundColor->setText(c.name());
	}
}
void DatabaseViewer::selectEmptyColor()
{
	QColor c = QColorDialog::getColor(ui_->lineEdit_emptyColor->text(), this);
	if(c.isValid())
	{
		ui_->lineEdit_emptyColor->setText(c.name());
	}
}
void DatabaseViewer::editDepthImage()
{
	if(dbDriver_ && ids_.size())
	{
		if(lastSliderIndexBrowsed_>= ids_.size())
		{
			lastSliderIndexBrowsed_ = ui_->horizontalSlider_A->value();
		}
		int id = ids_.at(lastSliderIndexBrowsed_);
		SensorData data;
		dbDriver_->getNodeData(id, data, true, false, false, false);
		data.uncompressData();
		if(!data.depthRaw().empty())
		{
			editDepthArea_->setColorMap(lastSliderIndexBrowsed_ == ui_->horizontalSlider_B->value()?ui_->graphicsView_B->getDepthColorMap():ui_->graphicsView_A->getDepthColorMap());
			editDepthArea_->setImage(data.depthRaw(), data.imageRaw());
			if(editDepthDialog_->exec() == QDialog::Accepted && editDepthArea_->isModified())
			{
				cv::Mat depth = editDepthArea_->getModifiedImage();
				UASSERT(data.depthRaw().type() == depth.type());
				UASSERT(data.depthRaw().cols == depth.cols);
				UASSERT(data.depthRaw().rows == depth.rows);
				dbDriver_->updateDepthImage(id, depth);
				this->update3dView();
			}
		}
	}
}

void DatabaseViewer::exportPosesRaw()
{
	exportPoses(0);
}
void DatabaseViewer::exportPosesRGBDSLAMMotionCapture()
{
	exportPoses(1);
}
void DatabaseViewer::exportPosesRGBDSLAM()
{
	exportPoses(10);
}
void DatabaseViewer::exportPosesKITTI()
{
	exportPoses(2);
}
void DatabaseViewer::exportPosesTORO()
{
	exportPoses(3);
}
void DatabaseViewer::exportPosesG2O()
{
	exportPoses(4);
}
void DatabaseViewer::exportPosesKML()
{
	exportPoses(5);
}

void DatabaseViewer::exportPoses(int format)
{
	if(graphes_.empty())
	{
		this->updateGraphView();
		if(graphes_.empty() || ui_->horizontalSlider_iterations->maximum() != (int)graphes_.size()-1)
		{
			QMessageBox::warning(this, tr("Cannot export poses"), tr("No graph in database?!"));
			return;
		}
	}

	if(format == 5)
	{
		if(gpsValues_.empty() || gpsPoses_.empty())
		{
			QMessageBox::warning(this, tr("Cannot export poses"), tr("No GPS in database?!"));
		}
		else
		{
			std::map<int, rtabmap::Transform> graph = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());

			//align with ground truth for more meaningful results
			pcl::PointCloud<pcl::PointXYZ> cloud1, cloud2;
			cloud1.resize(graph.size());
			cloud2.resize(graph.size());
			int oi = 0;
			int idFirst = 0;
			for(std::map<int, Transform>::const_iterator iter=gpsPoses_.begin(); iter!=gpsPoses_.end(); ++iter)
			{
				std::map<int, Transform>::iterator iter2 = graph.find(iter->first);
				if(iter2!=graph.end())
				{
					if(oi==0)
					{
						idFirst = iter->first;
					}
					cloud1[oi] = pcl::PointXYZ(iter->second.x(), iter->second.y(), iter->second.z());
					cloud2[oi++] = pcl::PointXYZ(iter2->second.x(), iter2->second.y(), iter2->second.z());
				}
			}

			Transform t = Transform::getIdentity();
			if(oi>5)
			{
				cloud1.resize(oi);
				cloud2.resize(oi);

				t = util3d::transformFromXYZCorrespondencesSVD(cloud2, cloud1);
			}
			else if(idFirst)
			{
				t = gpsPoses_.at(idFirst) * graph.at(idFirst).inverse();
			}

			std::map<int, GPS> values;
			GeodeticCoords origin = gpsValues_.begin()->second.toGeodeticCoords();
			for(std::map<int, Transform>::iterator iter=graph.begin(); iter!=graph.end(); ++iter)
			{
				iter->second = t * iter->second;

				GeodeticCoords coord;
				coord.fromENU_WGS84(cv::Point3d(iter->second.x(), iter->second.y(), iter->second.z()), origin);
				double bearing = -(iter->second.theta()*180.0/M_PI-90.0);
				if(bearing < 0)
				{
					bearing += 360;
				}

				Transform p, g;
				int w;
				std::string l;
				double stamp=0.0;
				int mapId;
				std::vector<float> v;
				GPS gps;
				EnvSensors sensors;
				dbDriver_->getNodeInfo(iter->first, p, mapId, w, l, stamp, g, v, gps, sensors);
				values.insert(std::make_pair(iter->first, GPS(stamp, coord.longitude(), coord.latitude(), coord.altitude(), 0, 0)));
			}

			QString output = pathDatabase_ + QDir::separator() + "poses.kml";
			QString path = QFileDialog::getSaveFileName(
					this,
					tr("Save File"),
					output,
					tr("Google Earth file (*.kml)"));

			if(!path.isEmpty())
			{
				bool saved = graph::exportGPS(path.toStdString(), values, ui_->graphViewer->getNodeColor().rgba());

				if(saved)
				{
					QMessageBox::information(this,
							tr("Export poses..."),
							tr("GPS coordinates saved to \"%1\".")
							.arg(path));
				}
				else
				{
					QMessageBox::information(this,
							tr("Export poses..."),
							tr("Failed to save GPS coordinates to \"%1\"!")
							.arg(path));
				}
			}
		}
		return;
	}

	std::map<int, Transform> optimizedPoses;
	if(ui_->checkBox_alignScansCloudsWithGroundTruth->isChecked() && !groundTruthPoses_.empty())
	{
		optimizedPoses = groundTruthPoses_;
	}
	else
	{
		optimizedPoses = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());

		if(ui_->checkBox_alignPosesWithGroundTruth->isChecked())
		{
			std::map<int, Transform> refPoses = groundTruthPoses_;
			if(refPoses.empty())
			{
				refPoses = gpsPoses_;
			}

			// Log ground truth statistics (in TUM's RGBD-SLAM format)
			if(refPoses.size())
			{
				float translational_rmse = 0.0f;
				float translational_mean = 0.0f;
				float translational_median = 0.0f;
				float translational_std = 0.0f;
				float translational_min = 0.0f;
				float translational_max = 0.0f;
				float rotational_rmse = 0.0f;
				float rotational_mean = 0.0f;
				float rotational_median = 0.0f;
				float rotational_std = 0.0f;
				float rotational_min = 0.0f;
				float rotational_max = 0.0f;

				Transform gtToMap = graph::calcRMSE(
						refPoses,
						optimizedPoses,
						translational_rmse,
						translational_mean,
						translational_median,
						translational_std,
						translational_min,
						translational_max,
						rotational_rmse,
						rotational_mean,
						rotational_median,
						rotational_std,
						rotational_min,
						rotational_max);

				if(ui_->checkBox_alignPosesWithGroundTruth->isChecked() && !gtToMap.isIdentity())
				{
					for(std::map<int, Transform>::iterator iter=optimizedPoses.begin(); iter!=optimizedPoses.end(); ++iter)
					{
						iter->second = gtToMap * iter->second;
					}
				}
			}
		}
	}

	if(optimizedPoses.size())
	{
		std::map<int, Transform> localTransforms;
		QStringList items;
		items.push_back("Robot (base frame)");
		items.push_back("Camera");
		items.push_back("Scan");
		bool ok;
		QString item = QInputDialog::getItem(this, tr("Export Poses"), tr("Frame: "), items, 0, false, &ok);
		if(!ok || item.isEmpty())
		{
			return;
		}
		if(item.compare("Robot (base frame)") != 0)
		{
			bool cameraFrame = item.compare("Camera") == 0;
			for(std::map<int, Transform>::iterator iter=optimizedPoses.begin(); iter!=optimizedPoses.end(); ++iter)
			{
				Transform localTransform;
				if(cameraFrame)
				{
					std::vector<CameraModel> models;
					StereoCameraModel stereoModel;
					if(dbDriver_->getCalibration(iter->first, models, stereoModel))
					{
						if((models.size() == 1 &&
							!models.at(0).localTransform().isNull()))
						{
							localTransform = models.at(0).localTransform();
						}
						else if(!stereoModel.localTransform().isNull())
						{
							localTransform = stereoModel.localTransform();
						}
						else if(models.size()>1)
						{
							UWARN("Multi-camera is not supported (node %d)", iter->first);
						}
						else
						{
							UWARN("Calibration not valid for node %d", iter->first);
						}
					}
					else
					{
						UWARN("Missing calibration for node %d", iter->first);
					}
				}
				else
				{
					LaserScan info;
					if(dbDriver_->getLaserScanInfo(iter->first, info))
					{
						localTransform = info.localTransform();
					}
					else
					{
						UWARN("Missing scan info for node %d", iter->first);
					}

				}
				if(!localTransform.isNull())
				{
					localTransforms.insert(std::make_pair(iter->first, localTransform));
				}
			}
			if(localTransforms.empty())
			{
				QMessageBox::warning(this,
						tr("Export Poses"),
						tr("Could not find any \"%1\" frame, exporting in Robot frame instead.").arg(item));
			}
		}

		std::map<int, Transform> poses;
		std::multimap<int, Link> links;
		if(localTransforms.empty())
		{
			poses = optimizedPoses;
			links = graphLinks_;
		}
		else
		{
			//adjust poses and links
			for(std::map<int, Transform>::iterator iter=localTransforms.begin(); iter!=localTransforms.end(); ++iter)
			{
				poses.insert(std::make_pair(iter->first, optimizedPoses.at(iter->first) * iter->second));
			}
			for(std::multimap<int, Link>::iterator iter=graphLinks_.begin(); iter!=graphLinks_.end(); ++iter)
			{
				if(uContains(poses, iter->second.from()) && uContains(poses, iter->second.to()))
				{
					std::multimap<int, Link>::iterator inserted = links.insert(*iter);
					int from = iter->second.from();
					int to = iter->second.to();
					inserted->second.setTransform(localTransforms.at(from).inverse()*iter->second.transform()*localTransforms.at(to));
				}
			}
		}

		std::map<int, double> stamps;
		if(format == 1 || format == 10)
		{
			for(std::map<int, Transform>::iterator iter=poses.begin(); iter!=poses.end(); ++iter)
			{
				Transform p, g;
				int w;
				std::string l;
				double stamp=0.0;
				int mapId;
				std::vector<float> v;
				GPS gps;
				EnvSensors sensors;
				if(dbDriver_->getNodeInfo(iter->first, p, mapId, w, l, stamp, g, v, gps, sensors))
				{
					stamps.insert(std::make_pair(iter->first, stamp));
				}
			}
			if(stamps.size()!=poses.size())
			{
				QMessageBox::warning(this, tr("Export poses..."), tr("Poses (%1) and stamps (%2) have not the same size! Cannot export in RGB-D SLAM format.")
						.arg(poses.size()).arg(stamps.size()));
				return;
			}
		}

		QString output = pathDatabase_ + QDir::separator() + (format==3?"toro.graph":format==4?"poses.g2o":"poses.txt");

		QString path = QFileDialog::getSaveFileName(
				this,
				tr("Save File"),
				output,
				format == 3?tr("TORO file (*.graph)"):format==4?tr("g2o file (*.g2o)"):tr("Text file (*.txt)"));

		if(!path.isEmpty())
		{
			if(QFileInfo(path).suffix() == "")
			{
				if(format == 3)
				{
					path += ".graph";
				}
				else if(format==4)
				{
					path += ".g2o";
				}
				else
				{
					path += ".txt";
				}
			}

			bool saved = graph::exportPoses(path.toStdString(), format, poses, links, stamps, ui_->parameters_toolbox->getParameters());

			if(saved)
			{
				QMessageBox::information(this,
						tr("Export poses..."),
						tr("%1 saved to \"%2\".")
						.arg(format == 3?"TORO graph":format == 4?"g2o graph":"Poses")
						.arg(path));
			}
			else
			{
				QMessageBox::information(this,
						tr("Export poses..."),
						tr("Failed to save %1 to \"%2\"!")
						.arg(format == 3?"TORO graph":format == 4?"g2o graph":"poses")
						.arg(path));
			}
		}
	}
}

void DatabaseViewer::exportGPS_TXT()
{
	exportGPS(0);
}
void DatabaseViewer::exportGPS_KML()
{
	exportGPS(1);
}

void DatabaseViewer::exportGPS(int format)
{
	if(!gpsValues_.empty())
	{
		QString output = pathDatabase_ + QDir::separator() + (format==0?"gps.txt":"gps.kml");
		QString path = QFileDialog::getSaveFileName(
				this,
				tr("Save File"),
				output,
				format==0?tr("Raw format (*.txt)"):tr("Google Earth file (*.kml)"));

		if(!path.isEmpty())
		{
			bool saved = graph::exportGPS(path.toStdString(), gpsValues_, ui_->graphViewer->getGPSColor().rgba());

			if(saved)
			{
				QMessageBox::information(this,
						tr("Export poses..."),
						tr("GPS coordinates saved to \"%1\".")
						.arg(path));
			}
			else
			{
				QMessageBox::information(this,
						tr("Export poses..."),
						tr("Failed to save GPS coordinates to \"%1\"!")
						.arg(path));
			}
		}
	}
}

void DatabaseViewer::editSaved2DMap()
{
	if(!dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot edit 2D map"), tr("A database must must loaded first...\nUse File->Open database."));
		return;
	}

	if(uStrNumCmp(dbDriver_->getDatabaseVersion(), "0.17.0") < 0)
	{
		QMessageBox::warning(this, tr("Cannot edit 2D map"),
				tr("The database has too old version (%1) to saved "
					"optimized map. Version 0.17.0 minimum required.").arg(dbDriver_->getDatabaseVersion().c_str()));
		return;
	}

	if(linksAdded_.size() || linksRefined_.size() || linksRemoved_.size() || generatedLocalMaps_.size())
	{
		QMessageBox::warning(this, tr("Cannot edit 2D map"),
				tr("The database has modified links and/or modified local "
				   "occupancy grids, the 2D optimized map cannot be modified."));
		return;
	}

	float xMin, yMin, cellSize;
	cv::Mat map = dbDriver_->load2DMap(xMin, yMin, cellSize);
	if(map.empty())
	{
		QMessageBox::warning(this, tr("Cannot export 2D map"), tr("The database doesn't contain a saved 2D map."));
		return;
	}

	cv::Mat map8U = rtabmap::util3d::convertMap2Image8U(map, false);
	cv::Mat map8UFlip, map8URotated;
	cv::flip(map8U, map8UFlip, 0);
	if(!ui_->graphViewer->isOrientationENU())
	{
		//ROTATE_90_COUNTERCLOCKWISE
		cv::transpose(map8UFlip, map8URotated);
		cv::flip(map8URotated, map8URotated, 0);
	}
	else
	{
		map8URotated = map8UFlip;
	}

	editMapArea_->setMap(map8URotated);
	if(editMapDialog_->exec() == QDialog::Accepted && editMapArea_->isModified())
	{
		cv::Mat map = editMapArea_->getModifiedMap();

		if(!ui_->graphViewer->isOrientationENU())
		{
			//ROTATE_90_CLOCKWISE
			cv::transpose(map, map8URotated);
			cv::flip(map8URotated, map8URotated, 1);
		}
		else
		{
			map8URotated = map;
		}
		cv::flip(map8URotated, map8UFlip, 0);

		UASSERT(map8UFlip.type() == map8U.type());
		UASSERT(map8UFlip.cols == map8U.cols);
		UASSERT(map8UFlip.rows == map8U.rows);

		dbDriver_->save2DMap(rtabmap::util3d::convertImage8U2Map(map8UFlip, false), xMin, yMin, cellSize);
		QMessageBox::information(this, tr("Edit 2D map"), tr("Map updated!"));
	}
}

void DatabaseViewer::exportSaved2DMap()
{
	if(!dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot export 2D map"), tr("A database must must loaded first...\nUse File->Open database."));
		return;
	}

	float xMin, yMin, cellSize;
	cv::Mat map = dbDriver_->load2DMap(xMin, yMin, cellSize);
	if(map.empty())
	{
		QMessageBox::warning(this, tr("Cannot export 2D map"), tr("The database doesn't contain a saved 2D map."));
	}
	else
	{
		cv::Mat map8U = rtabmap::util3d::convertMap2Image8U(map, true);
		QString name = QFileInfo(databaseFileName_.c_str()).baseName();
		QString path = QFileDialog::getSaveFileName(
				this,
				tr("Save File"),
				pathDatabase_+"/" + name + ".pgm",
				tr("Map (*.pgm)"));

		if(!path.isEmpty())
		{
			if(QFileInfo(path).suffix() == "")
			{
				path += ".pgm";
			}
			cv::imwrite(path.toStdString(), map8U);
			QMessageBox::information(this, tr("Export 2D map"), tr("Exported %1!").arg(path));
		}
	}
}

void DatabaseViewer::import2DMap()
{
	if(!dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot import 2D map"), tr("A database must must loaded first...\nUse File->Open database."));
		return;
	}

	if(uStrNumCmp(dbDriver_->getDatabaseVersion(), "0.17.0") < 0)
	{
		QMessageBox::warning(this, tr("Cannot edit 2D map"),
				tr("The database has too old version (%1) to saved "
					"optimized map. Version 0.17.0 minimum required.").arg(dbDriver_->getDatabaseVersion().c_str()));
		return;
	}

	if(linksAdded_.size() || linksRefined_.size() || linksRemoved_.size() || generatedLocalMaps_.size())
	{
		QMessageBox::warning(this, tr("Cannot import 2D map"),
				tr("The database has modified links and/or modified local "
				   "occupancy grids, the 2D optimized map cannot be modified."));
		return;
	}

	float xMin, yMin, cellSize;
	cv::Mat mapOrg = dbDriver_->load2DMap(xMin, yMin, cellSize);
	if(mapOrg.empty())
	{
		QMessageBox::warning(this, tr("Cannot import 2D map"), tr("The database doesn't contain a saved 2D map."));
	}
	else
	{
		QString path = QFileDialog::getOpenFileName(
						this,
						tr("Open File"),
						pathDatabase_,
						tr("Map (*.pgm)"));
		if(!path.isEmpty())
		{
			cv::Mat map8U = cv::imread(path.toStdString(), cv::IMREAD_UNCHANGED);
			cv::Mat map = rtabmap::util3d::convertImage8U2Map(map8U, true);

			if(mapOrg.cols == map.cols && mapOrg.rows == map8U.rows)
			{
				dbDriver_->save2DMap(map, xMin, yMin, cellSize);
				QMessageBox::information(this, tr("Import 2D map"), tr("Imported %1!").arg(path));
			}
			else
			{
				QMessageBox::warning(this, tr("Import 2D map"), tr("Cannot import %1 as its size doesn't match the current saved map. Import 2D Map action should only be used to modify the map saved in the database.").arg(path));
			}
		}
	}
}

void DatabaseViewer::regenerateSavedMap()
{
	if(!dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot import 2D map"), tr("A database must must loaded first...\nUse File->Open database."));
		return;
	}

	if(uStrNumCmp(dbDriver_->getDatabaseVersion(), "0.17.0") < 0)
	{
		QMessageBox::warning(this, tr("Cannot edit 2D map"),
				tr("The database has too old version (%1) to saved "
					"optimized map. Version 0.17.0 minimum required.").arg(dbDriver_->getDatabaseVersion().c_str()));
		return;
	}

	if(linksAdded_.size() || linksRefined_.size() || linksRemoved_.size() || generatedLocalMaps_.size())
	{
		QMessageBox::warning(this, tr("Cannot import 2D map"),
				tr("The database has modified links and/or modified local "
				   "occupancy grids, the 2D optimized map cannot be modified."));
		return;
	}

	if((int)graphes_.empty() || localMaps_.empty())
	{
		QMessageBox::warning(this, tr("Cannot regenerate 2D map"),
				tr("Graph is empty, make sure you opened the "
				   "Graph View and there is a map shown."));
		return;
	}


	//update scans
	UINFO("Update local maps list...");
	OccupancyGrid grid(ui_->parameters_toolbox->getParameters());
	for(std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat> >::iterator iter=localMaps_.begin(); iter!=localMaps_.end(); ++iter)
	{
		grid.addToCache(iter->first, iter->second.first.first, iter->second.first.second, iter->second.second);
	}
	grid.update(graphes_.back());
	float xMin, yMin;
	cv::Mat map = grid.getMap(xMin, yMin);

	if(map.empty())
	{
		QMessageBox::information(this, tr("Regenerate 2D map"), tr("Failed to renegerate the map, resulting map is empty!"));
	}
	else
	{
		dbDriver_->save2DMap(map, xMin, yMin, grid.getCellSize());
		QMessageBox::information(this, tr("Regenerate 2D map"), tr("Map regenerated!"));
		ui_->actionEdit_optimized_2D_map->setEnabled(true);
		ui_->actionExport_saved_2D_map->setEnabled(true);
		ui_->actionImport_2D_map->setEnabled(true);
	}
}

void DatabaseViewer::viewOptimizedMesh()
{
	if(!dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot view optimized mesh"), tr("A database must must loaded first...\nUse File->Open database."));
		return;
	}

	std::vector<std::vector<std::vector<unsigned int> > > polygons;
#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
	std::vector<std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > > texCoords;
#else
	std::vector<std::vector<Eigen::Vector2f> > texCoords;
#endif
	cv::Mat textures;
	cv::Mat cloudMat = dbDriver_->loadOptimizedMesh(&polygons, &texCoords, &textures);
	if(cloudMat.empty())
	{
		QMessageBox::warning(this, tr("Cannot view optimized mesh"), tr("The database doesn't contain a saved optimized mesh."));
	}
	else
	{
		CloudViewer * viewer = new CloudViewer(this);
		viewer->setWindowFlags(Qt::Window);
		viewer->setAttribute(Qt::WA_DeleteOnClose);
		viewer->buildPickingLocator(true);
		if(!textures.empty())
		{
			pcl::TextureMeshPtr mesh = util3d::assembleTextureMesh(cloudMat, polygons, texCoords, textures, true);
			util3d::fixTextureMeshForVisualization(*mesh);
			viewer->setWindowTitle("Optimized Textured Mesh");
			viewer->setPolygonPicking(true);
			viewer->addCloudTextureMesh("mesh", mesh, textures);
		}
		else if(polygons.size() == 1)
		{
			pcl::PolygonMeshPtr mesh = util3d::assemblePolygonMesh(cloudMat, polygons.at(0));
			viewer->setWindowTitle("Optimized Mesh");
			viewer->setPolygonPicking(true);
			viewer->addCloudMesh("mesh", mesh);
		}
		else
		{
			LaserScan scan = LaserScan::backwardCompatibility(cloudMat);
			pcl::PCLPointCloud2::Ptr cloud = util3d::laserScanToPointCloud2(scan);
			viewer->setWindowTitle("Optimized Point Cloud");
			viewer->addCloud("mesh", cloud, Transform::getIdentity(), scan.hasRGB(), scan.hasNormals(), scan.hasIntensity());
		}
		viewer->show();
	}
}

void DatabaseViewer::exportOptimizedMesh()
{
	if(!dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot export optimized mesh"), tr("A database must must loaded first...\nUse File->Open database."));
		return;
	}

	std::vector<std::vector<std::vector<unsigned int> > > polygons;
#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
	std::vector<std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > > texCoords;
#else
	std::vector<std::vector<Eigen::Vector2f> > texCoords;
#endif
	cv::Mat textures;
	cv::Mat cloudMat = dbDriver_->loadOptimizedMesh(&polygons, &texCoords, &textures);
	if(cloudMat.empty())
	{
		QMessageBox::warning(this, tr("Cannot export optimized mesh"), tr("The database doesn't contain a saved optimized mesh."));
	}
	else
	{
		QString name = QFileInfo(databaseFileName_.c_str()).baseName();

		if(!textures.empty())
		{
			pcl::TextureMeshPtr mesh = util3d::assembleTextureMesh(cloudMat, polygons, texCoords, textures);
			QString path = QFileDialog::getSaveFileName(
					this,
					tr("Save File"),
					pathDatabase_+"/" + name + ".obj",
					tr("Mesh (*.obj)"));

			if(!path.isEmpty())
			{
				if(QFileInfo(path).suffix() == "")
				{
					path += ".obj";
				}
				QString baseName = QFileInfo(path).baseName();
				if(mesh->tex_materials.size() == 1)
				{
					mesh->tex_materials.at(0).tex_file = baseName.toStdString() + ".png";
					cv::imwrite((QFileInfo(path).absoluteDir().absolutePath()+QDir::separator()+baseName).toStdString() + ".png", textures);
				}
				else
				{
					for(unsigned int i=0; i<mesh->tex_materials.size(); ++i)
					{
						mesh->tex_materials.at(i).tex_file = (baseName+QDir::separator()+QString::number(i)+".png").toStdString();
						UASSERT((i+1)*textures.rows <= (unsigned int)textures.cols);
						cv::imwrite((QFileInfo(path).absoluteDir().absolutePath()+QDir::separator()+baseName+QDir::separator()+QString::number(i)+".png").toStdString(), textures(cv::Range::all(), cv::Range(i*textures.rows, (i+1)*textures.rows)));
					}
				}
				pcl::io::saveOBJFile(path.toStdString(), *mesh);

				QMessageBox::information(this, tr("Export Textured Mesh"), tr("Exported %1!").arg(path));
			}
		}
		else if(polygons.size() == 1)
		{
			pcl::PolygonMeshPtr mesh = util3d::assemblePolygonMesh(cloudMat, polygons.at(0));
			QString path = QFileDialog::getSaveFileName(
					this,
					tr("Save File"),
					pathDatabase_+"/" + name + ".ply",
					tr("Mesh (*.ply)"));

			if(!path.isEmpty())
			{
				if(QFileInfo(path).suffix() == "")
				{
					path += ".ply";
				}
				pcl::io::savePLYFileBinary(path.toStdString(), *mesh);
				QMessageBox::information(this, tr("Export Mesh"), tr("Exported %1!").arg(path));
			}
		}
		else
		{
			QString path = QFileDialog::getSaveFileName(
					this,
					tr("Save File"),
					pathDatabase_+"/" + name + ".ply",
					tr("Point cloud data (*.ply *.pcd)"));

			if(!path.isEmpty())
			{
				if(QFileInfo(path).suffix() == "")
				{
					path += ".ply";
				}
				bool success = false;
				pcl::PCLPointCloud2::Ptr cloud = util3d::laserScanToPointCloud2(LaserScan::backwardCompatibility(cloudMat));
				if(QFileInfo(path).suffix() == "pcd")
				{
					success = pcl::io::savePCDFile(path.toStdString(), *cloud) == 0;
				}
				else
				{
					success = pcl::io::savePLYFile(path.toStdString(), *cloud) == 0;
				}
				if(success)
				{
					QMessageBox::information(this, tr("Export Point Cloud"), tr("Exported %1!").arg(path));
				}
				else
				{
					QMessageBox::critical(this, tr("Export Point Cloud"), tr("Failed exporting %1!").arg(path));
				}
			}
		}
	}
}

void DatabaseViewer::updateOptimizedMesh()
{
	if(!ids_.size() || !dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot generate a graph"), tr("The database is empty..."));
		return;
	}

	if(graphes_.empty())
	{
		this->updateGraphView();
		if(graphes_.empty() || ui_->horizontalSlider_iterations->maximum() != (int)graphes_.size()-1)
		{
			QMessageBox::warning(this, tr("Cannot generate a graph"), tr("No graph in database?!"));
			return;
		}
	}

	std::map<int, Transform> optimizedPoses;
	if(ui_->checkBox_alignScansCloudsWithGroundTruth->isChecked() && !groundTruthPoses_.empty())
	{
		optimizedPoses = groundTruthPoses_;
	}
	else
	{
		optimizedPoses = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());
	}
	if(ui_->groupBox_posefiltering->isChecked())
	{
		optimizedPoses = graph::radiusPosesFiltering(optimizedPoses,
				ui_->doubleSpinBox_posefilteringRadius->value(),
				ui_->doubleSpinBox_posefilteringAngle->value()*CV_PI/180.0);
	}
	if(optimizedPoses.size() > 0)
	{
		exportDialog_->setDBDriver(dbDriver_);
		exportDialog_->forceAssembling(true);
		exportDialog_->setOkButton();

		std::map<int, pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr> clouds;
		std::map<int, pcl::PolygonMesh::Ptr> meshes;
		std::map<int, pcl::TextureMesh::Ptr> textureMeshes;
		std::vector<std::map<int, pcl::PointXY> > textureVertexToPixels;

		if(exportDialog_->getExportedClouds(
				optimizedPoses,
				updateLinksWithModifications(links_),
				mapIds_,
				QMap<int, Signature>(),
				std::map<int, std::pair<pcl::PointCloud<pcl::PointXYZRGB>::Ptr, pcl::IndicesPtr> >(),
				std::map<int, LaserScan>(),
				pathDatabase_,
				ui_->parameters_toolbox->getParameters(),
				clouds,
				meshes,
				textureMeshes,
				textureVertexToPixels))
		{
			if(textureMeshes.size())
			{
				dbDriver_->saveOptimizedPoses(optimizedPoses, Transform());

				cv::Mat globalTextures;
				pcl::TextureMeshPtr textureMesh = textureMeshes.at(0);
				if(textureMesh->tex_materials.size()>1)
				{
					globalTextures = util3d::mergeTextures(
							*textureMesh,
							std::map<int, cv::Mat>(),
							std::map<int, std::vector<CameraModel> >(),
							0,
							dbDriver_,
							exportDialog_->getTextureSize(),
							exportDialog_->getMaxTextures(),
							textureVertexToPixels,
							exportDialog_->isGainCompensation(),
							exportDialog_->getGainBeta(),
							exportDialog_->isGainRGB(),
							exportDialog_->isBlending(),
							exportDialog_->getBlendingDecimation(),
							exportDialog_->getTextureBrightnessConstrastRatioLow(),
							exportDialog_->getTextureBrightnessConstrastRatioHigh(),
							exportDialog_->isExposeFusion());
				}
				dbDriver_->saveOptimizedMesh(
						util3d::laserScanFromPointCloud(textureMesh->cloud, false).data(),
						util3d::convertPolygonsFromPCL(textureMesh->tex_polygons),
						textureMesh->tex_coordinates,
						globalTextures);
				QMessageBox::information(this, tr("Update Optimized Textured Mesh"), tr("Updated!"));
				ui_->actionView_optimized_mesh->setEnabled(true);
				ui_->actionExport_optimized_mesh->setEnabled(true);
				this->viewOptimizedMesh();
			}
			else if(meshes.size())
			{
				dbDriver_->saveOptimizedPoses(optimizedPoses, Transform());
				std::vector<std::vector<std::vector<unsigned int> > > polygons(1);
				polygons.at(0) = util3d::convertPolygonsFromPCL(meshes.at(0)->polygons);
				dbDriver_->saveOptimizedMesh(util3d::laserScanFromPointCloud(meshes.at(0)->cloud, false).data(), polygons);
				QMessageBox::information(this, tr("Update Optimized Mesh"), tr("Updated!"));
				ui_->actionView_optimized_mesh->setEnabled(true);
				ui_->actionExport_optimized_mesh->setEnabled(true);
				this->viewOptimizedMesh();
			}
			else if(clouds.size())
			{
				dbDriver_->saveOptimizedPoses(optimizedPoses, Transform());
				dbDriver_->saveOptimizedMesh(util3d::laserScanFromPointCloud(*clouds.at(0)));
				QMessageBox::information(this, tr("Update Optimized PointCloud"), tr("Updated!"));
				ui_->actionView_optimized_mesh->setEnabled(true);
				ui_->actionExport_optimized_mesh->setEnabled(true);
				this->viewOptimizedMesh();
			}
			else
			{
				QMessageBox::critical(this, tr("Update Optimized Mesh"), tr("Nothing to save!"));
			}
		}
		exportDialog_->setProgressDialogToMax();
	}
	else
	{
		QMessageBox::critical(this, tr("Error"), tr("No neighbors found for node %1.").arg(ui_->spinBox_optimizationsFrom->value()));
	}
}

void DatabaseViewer::generateGraph()
{
	if(!dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot generate a graph"), tr("A database must must loaded first...\nUse File->Open database."));
		return;
	}

	QString path = QFileDialog::getSaveFileName(this, tr("Save File"), pathDatabase_+"/Graph.dot", tr("Graphiz file (*.dot)"));
	if(!path.isEmpty())
	{
		dbDriver_->generateGraph(path.toStdString());
	}
}

void DatabaseViewer::generateLocalGraph()
{
	if(!ids_.size() || !dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot generate a graph"), tr("The database is empty..."));
		return;
	}
	bool ok = false;
	int id = QInputDialog::getInt(this, tr("Around which location?"), tr("Location ID"), ids_.first(), ids_.first(), ids_.last(), 1, &ok);

	if(ok)
	{
		int margin = QInputDialog::getInt(this, tr("Depth around the location?"), tr("Margin"), 4, 1, 100, 1, &ok);
		if(ok)
		{
			QString path = QFileDialog::getSaveFileName(this, tr("Save File"), pathDatabase_+"/Graph" + QString::number(id) + ".dot", tr("Graphiz file (*.dot)"));
			if(!path.isEmpty() && id>0)
			{
				std::map<int, int> ids;
				std::list<int> curentMarginList;
				std::set<int> currentMargin;
				std::set<int> nextMargin;
				nextMargin.insert(id);
				int m = 0;
				while((margin == 0 || m < margin) && nextMargin.size())
				{
					curentMarginList = std::list<int>(nextMargin.rbegin(), nextMargin.rend());
					nextMargin.clear();

					for(std::list<int>::iterator jter = curentMarginList.begin(); jter!=curentMarginList.end(); ++jter)
					{
						if(ids.find(*jter) == ids.end())
						{
							std::multimap<int, Link> links;
							ids.insert(std::pair<int, int>(*jter, m));

							UTimer timer;
							dbDriver_->loadLinks(*jter, links);

							// links
							for(std::multimap<int, Link>::const_iterator iter=links.begin(); iter!=links.end(); ++iter)
							{
								if( !uContains(ids, iter->first))
								{
									UASSERT(iter->second.type() != Link::kUndef);
									if(iter->second.type() == Link::kNeighbor ||
									   iter->second.type() == Link::kNeighborMerged)
									{
										nextMargin.insert(iter->first);
									}
									else
									{
										// loop closures are on same margin
										if(currentMargin.insert(iter->first).second)
										{
											curentMarginList.push_back(iter->first);
										}
									}
								}
							}
						}
					}
					++m;
				}

				if(ids.size() > 0)
				{
					ids.insert(std::pair<int,int>(id, 0));
					std::set<int> idsSet;
					for(std::map<int, int>::iterator iter = ids.begin(); iter!=ids.end(); ++iter)
					{
						idsSet.insert(idsSet.end(), iter->first);
						UINFO("Node %d", iter->first);
					}
					UINFO("idsSet=%d", idsSet.size());
					dbDriver_->generateGraph(path.toStdString(), idsSet);
				}
				else
				{
					QMessageBox::critical(this, tr("Error"), tr("No neighbors found for signature %1.").arg(id));
				}
			}
		}
	}
}

void DatabaseViewer::regenerateLocalMaps()
{
	OccupancyGrid grid(ui_->parameters_toolbox->getParameters());

	generatedLocalMaps_.clear();
	generatedLocalMapsInfo_.clear();

	rtabmap::ProgressDialog progressDialog(this);
	progressDialog.setMaximumSteps(ids_.size());
	progressDialog.show();
	progressDialog.setCancelButtonVisible(true);

	UPlot * plot = new UPlot(this);
	plot->setWindowFlags(Qt::Window);
	plot->setWindowTitle("Local Occupancy Grid Generation Time (ms)");
	plot->setAttribute(Qt::WA_DeleteOnClose);
	UPlotCurve * decompressionCurve = plot->addCurve("Decompression");
	UPlotCurve * gridCreationCurve = plot->addCurve("Grid Creation");
	plot->show();

	UPlot * plotCells = new UPlot(this);
	plotCells->setWindowFlags(Qt::Window);
	plotCells->setWindowTitle("Occupancy Cells");
	plotCells->setAttribute(Qt::WA_DeleteOnClose);
	UPlotCurve * totalCurve = plotCells->addCurve("Total");
	UPlotCurve * emptyCurve = plotCells->addCurve("Empty");
	UPlotCurve * obstaclesCurve = plotCells->addCurve("Obstacles");
	UPlotCurve * groundCurve = plotCells->addCurve("Ground");
	plotCells->show();

	double decompressionTime = 0;
	double gridCreationTime = 0;

	for(int i =0; i<ids_.size() && !progressDialog.isCanceled(); ++i)
	{
		UTimer timer;
		SensorData data;
		dbDriver_->getNodeData(ids_.at(i), data);
		data.uncompressData();
		decompressionTime = timer.ticks()*1000.0;

		int mapId, weight;
		Transform odomPose, groundTruth;
		std::string label;
		double stamp;
		QString msg;
		std::vector<float> velocity;
		GPS gps;
		EnvSensors sensors;
		if(dbDriver_->getNodeInfo(data.id(), odomPose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors))
		{
			Signature s = data;
			s.setPose(odomPose);
			cv::Mat ground, obstacles, empty;
			cv::Point3f viewpoint;
			timer.ticks();

			if(ui_->checkBox_grid_regenerateFromSavedGrid->isChecked() && s.sensorData().gridCellSize() > 0.0f)
			{
				pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud = util3d::laserScanToPointCloudRGB(LaserScan::backwardCompatibility(s.sensorData().gridObstacleCellsRaw()));
				*cloud+=*util3d::laserScanToPointCloudRGB(LaserScan::backwardCompatibility(s.sensorData().gridGroundCellsRaw()));

				if(cloud->size())
				{
					// update viewpoint
					if(s.sensorData().cameraModels().size())
					{
						// average of all local transforms
						float sum = 0;
						for(unsigned int i=0; i<s.sensorData().cameraModels().size(); ++i)
						{
							const Transform & t = s.sensorData().cameraModels()[i].localTransform();
							if(!t.isNull())
							{
								viewpoint.x += t.x();
								viewpoint.y += t.y();
								viewpoint.z += t.z();
								sum += 1.0f;
							}
						}
						if(sum > 0.0f)
						{
							viewpoint.x /= sum;
							viewpoint.y /= sum;
							viewpoint.z /= sum;
						}
					}
					else
					{
						const Transform & t = s.sensorData().stereoCameraModel().localTransform();
						viewpoint = cv::Point3f(t.x(), t.y(), t.z());
					}

					grid.createLocalMap(LaserScan::backwardCompatibility(util3d::laserScanFromPointCloud(*cloud)), s.getPose(), ground, obstacles, empty, viewpoint);
				}
			}
			else
			{
				grid.createLocalMap(s, ground, obstacles, empty, viewpoint);
			}

			gridCreationTime = timer.ticks()*1000.0;
			uInsert(generatedLocalMaps_, std::make_pair(data.id(), std::make_pair(std::make_pair(ground, obstacles), empty)));
			uInsert(generatedLocalMapsInfo_, std::make_pair(data.id(), std::make_pair(grid.getCellSize(), viewpoint)));
			msg = QString("Generated local occupancy grid map %1/%2").arg(i+1).arg((int)ids_.size());

			totalCurve->addValue(ids_.at(i), obstacles.cols+ground.cols+empty.cols);
			emptyCurve->addValue(ids_.at(i), empty.cols);
			obstaclesCurve->addValue(ids_.at(i), obstacles.cols);
			groundCurve->addValue(ids_.at(i), ground.cols);
		}

		progressDialog.appendText(msg);
		progressDialog.incrementStep();

		decompressionCurve->addValue(ids_.at(i), decompressionTime);
		gridCreationCurve->addValue(ids_.at(i), gridCreationTime);

		if(ids_.size() < 50 || (i+1) % 25 == 0)
		{
			QApplication::processEvents();
		}
	}
	progressDialog.setValue(progressDialog.maximumSteps());

	if(graphes_.size())
	{
		update3dView();
		sliderIterationsValueChanged((int)graphes_.size()-1);
	}
	else
	{
		updateGrid();
	}
}

void DatabaseViewer::regenerateCurrentLocalMaps()
{
	UTimer time;
	OccupancyGrid grid(ui_->parameters_toolbox->getParameters());

	if(ids_.size() == 0)
	{
		UWARN("ids_ is empty!");
		return;
	}

	QSet<int> idsSet;
	idsSet.insert(ids_.at(ui_->horizontalSlider_A->value()));
	idsSet.insert(ids_.at(ui_->horizontalSlider_B->value()));
	QList<int> ids = idsSet.toList();

	rtabmap::ProgressDialog progressDialog(this);
	progressDialog.setMaximumSteps(ids.size());
	progressDialog.show();

	for(int i =0; i<ids.size(); ++i)
	{
		generatedLocalMaps_.erase(ids.at(i));
		generatedLocalMapsInfo_.erase(ids.at(i));

		SensorData data;
		dbDriver_->getNodeData(ids.at(i), data);
		data.uncompressData();

		int mapId, weight;
		Transform odomPose, groundTruth;
		std::string label;
		double stamp;
		QString msg;
		std::vector<float> velocity;
		GPS gps;
		EnvSensors sensors;
		if(dbDriver_->getNodeInfo(data.id(), odomPose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors))
		{
			Signature s = data;
			s.setPose(odomPose);
			cv::Mat ground, obstacles, empty;
			cv::Point3f viewpoint;

			if(ui_->checkBox_grid_regenerateFromSavedGrid->isChecked() && s.sensorData().gridCellSize() > 0.0f)
			{
				pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud = util3d::laserScanToPointCloudRGB(LaserScan::backwardCompatibility(s.sensorData().gridObstacleCellsRaw()));
				*cloud+=*util3d::laserScanToPointCloudRGB(LaserScan::backwardCompatibility(s.sensorData().gridGroundCellsRaw()));

				if(cloud->size())
				{
					// update viewpoint
					if(s.sensorData().cameraModels().size())
					{
						// average of all local transforms
						float sum = 0;
						for(unsigned int i=0; i<s.sensorData().cameraModels().size(); ++i)
						{
							const Transform & t = s.sensorData().cameraModels()[i].localTransform();
							if(!t.isNull())
							{
								viewpoint.x += t.x();
								viewpoint.y += t.y();
								viewpoint.z += t.z();
								sum += 1.0f;
							}
						}
						if(sum > 0.0f)
						{
							viewpoint.x /= sum;
							viewpoint.y /= sum;
							viewpoint.z /= sum;
						}
					}
					else
					{
						const Transform & t = s.sensorData().stereoCameraModel().localTransform();
						viewpoint = cv::Point3f(t.x(), t.y(), t.z());
					}

					grid.createLocalMap(LaserScan::backwardCompatibility(util3d::laserScanFromPointCloud(*cloud)), s.getPose(), ground, obstacles, empty, viewpoint);
				}
			}
			else
			{
				grid.createLocalMap(s, ground, obstacles, empty, viewpoint);
			}


			uInsert(generatedLocalMaps_, std::make_pair(data.id(), std::make_pair(std::make_pair(ground, obstacles),empty)));
			uInsert(generatedLocalMapsInfo_, std::make_pair(data.id(), std::make_pair(grid.getCellSize(), viewpoint)));
			msg = QString("Generated local occupancy grid map %1/%2 (%3s)").arg(i+1).arg((int)ids.size()).arg(time.ticks());
		}

		progressDialog.appendText(msg);
		progressDialog.incrementStep();
		QApplication::processEvents();
	}
	progressDialog.setValue(progressDialog.maximumSteps());

	if(graphes_.size())
	{
		update3dView();
		sliderIterationsValueChanged((int)graphes_.size()-1);
	}
	else
	{
		updateGrid();
	}
}

void DatabaseViewer::view3DMap()
{
	if(!ids_.size() || !dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot view 3D map"), tr("The database is empty..."));
		return;
	}

	if(graphes_.empty())
	{
		this->updateGraphView();
		if(graphes_.empty() || ui_->horizontalSlider_iterations->maximum() != (int)graphes_.size()-1)
		{
			QMessageBox::warning(this, tr("Cannot generate a graph"), tr("No graph in database?!"));
			return;
		}
	}

	std::map<int, Transform> optimizedPoses;
	if(ui_->checkBox_alignScansCloudsWithGroundTruth->isChecked() && !groundTruthPoses_.empty())
	{
		optimizedPoses = groundTruthPoses_;
	}
	else
	{
		optimizedPoses = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());
	}
	if(ui_->groupBox_posefiltering->isChecked())
	{
		optimizedPoses = graph::radiusPosesFiltering(optimizedPoses,
				ui_->doubleSpinBox_posefilteringRadius->value(),
				ui_->doubleSpinBox_posefilteringAngle->value()*CV_PI/180.0);
	}
	if(optimizedPoses.size() > 0)
	{
		exportDialog_->setDBDriver(dbDriver_);
		exportDialog_->viewClouds(optimizedPoses,
				updateLinksWithModifications(links_),
				mapIds_,
				QMap<int, Signature>(),
				std::map<int, std::pair<pcl::PointCloud<pcl::PointXYZRGB>::Ptr, pcl::IndicesPtr> >(),
				std::map<int, LaserScan>(),
				pathDatabase_,
				ui_->parameters_toolbox->getParameters());
	}
	else
	{
		QMessageBox::critical(this, tr("Error"), tr("No neighbors found for node %1.").arg(ui_->spinBox_optimizationsFrom->value()));
	}
}

void DatabaseViewer::generate3DMap()
{
	if(!ids_.size() || !dbDriver_)
	{
		QMessageBox::warning(this, tr("Cannot generate a graph"), tr("The database is empty..."));
		return;
	}

	if(graphes_.empty())
	{
		this->updateGraphView();
		if(graphes_.empty() || ui_->horizontalSlider_iterations->maximum() != (int)graphes_.size()-1)
		{
			QMessageBox::warning(this, tr("Cannot generate a graph"), tr("No graph in database?!"));
			return;
		}
	}

	std::map<int, Transform> optimizedPoses;
	if(ui_->checkBox_alignScansCloudsWithGroundTruth->isChecked() && !groundTruthPoses_.empty())
	{
		optimizedPoses = groundTruthPoses_;
	}
	else
	{
		optimizedPoses = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());
	}
	if(ui_->groupBox_posefiltering->isChecked())
	{
		optimizedPoses = graph::radiusPosesFiltering(optimizedPoses,
				ui_->doubleSpinBox_posefilteringRadius->value(),
				ui_->doubleSpinBox_posefilteringAngle->value()*CV_PI/180.0);
	}
	if(optimizedPoses.size() > 0)
	{
		exportDialog_->setDBDriver(dbDriver_);
		exportDialog_->exportClouds(optimizedPoses,
				updateLinksWithModifications(links_),
				mapIds_,
				QMap<int, Signature>(),
				std::map<int, std::pair<pcl::PointCloud<pcl::PointXYZRGB>::Ptr, pcl::IndicesPtr> >(),
				std::map<int, LaserScan>(),
				pathDatabase_,
				ui_->parameters_toolbox->getParameters());
	}
	else
	{
		QMessageBox::critical(this, tr("Error"), tr("No neighbors found for node %1.").arg(ui_->spinBox_optimizationsFrom->value()));
	}
}

void DatabaseViewer::detectMoreLoopClosures()
{
	if(graphes_.empty())
	{
		this->updateGraphView();
		if(graphes_.empty() || ui_->horizontalSlider_iterations->maximum() != (int)graphes_.size()-1)
		{
			QMessageBox::warning(this, tr("Cannot generate a graph"), tr("No graph in database?!"));
			return;
		}
	}

	const std::map<int, Transform> & optimizedPoses = graphes_.back();

	rtabmap::ProgressDialog * progressDialog = new rtabmap::ProgressDialog(this);
	progressDialog->setAttribute(Qt::WA_DeleteOnClose);
	progressDialog->setMaximumSteps(1);
	progressDialog->setCancelButtonVisible(true);
	progressDialog->setMinimumWidth(800);
	progressDialog->show();

	const ParametersMap & parameters = ui_->parameters_toolbox->getParameters();
	bool loopCovLimited = Parameters::defaultRGBDLoopCovLimited();
	Parameters::parse(parameters, Parameters::kRGBDLoopCovLimited(), loopCovLimited);
	if(loopCovLimited)
	{
		odomMaxInf_ = graph::getMaxOdomInf(updateLinksWithModifications(links_));
	}

	int iterations = ui_->spinBox_detectMore_iterations->value();
	UASSERT(iterations > 0);
	int added = 0;
	std::multimap<int, int> checkedLoopClosures;
	std::pair<int, int> lastAdded(0,0);
	bool intraSession = ui_->checkBox_detectMore_intraSession->isChecked();
	bool interSession = ui_->checkBox_detectMore_interSession->isChecked();
	if(!interSession && !intraSession)
	{
		QMessageBox::warning(this, tr("Cannot detect more loop closures"), tr("Intra and inter session parameters are disabled! Enable one or both."));
		return;
	}
	for(int n=0; n<iterations; ++n)
	{
		UINFO("iteration %d/%d", n+1, iterations);
		std::multimap<int, int> clusters = rtabmap::graph::radiusPosesClustering(
				std::map<int, Transform>(optimizedPoses.upper_bound(0), optimizedPoses.end()),
				ui_->doubleSpinBox_detectMore_radius->value(),
				ui_->doubleSpinBox_detectMore_angle->value()*CV_PI/180.0);

		progressDialog->setMaximumSteps(progressDialog->maximumSteps()+(int)clusters.size());
		progressDialog->appendText(tr("Looking for more loop closures, clusters found %1 clusters.").arg(clusters.size()));
		QApplication::processEvents();
		if(progressDialog->isCanceled())
		{
			break;
		}

		std::set<int> addedLinks;
		int i=0;
		for(std::multimap<int, int>::iterator iter=clusters.begin(); iter!= clusters.end() && !progressDialog->isCanceled(); ++iter, ++i)
		{
			int from = iter->first;
			int to = iter->second;
			if(from < to)
			{
				from = iter->second;
				to = iter->first;
			}

			int mapIdFrom = uValue(mapIds_, from, 0);
			int mapIdTo = uValue(mapIds_, to, 0);

			if((interSession && mapIdFrom != mapIdTo) ||
		       (intraSession && mapIdFrom == mapIdTo))
			{
				// only add new links and one per cluster per iteration
				if(rtabmap::graph::findLink(checkedLoopClosures, from, to) == checkedLoopClosures.end())
				{
					if(!findActiveLink(from, to).isValid() && !containsLink(linksRemoved_, from, to) &&
					   addedLinks.find(from) == addedLinks.end() &&
					   addedLinks.find(to) == addedLinks.end())
					{
						checkedLoopClosures.insert(std::make_pair(from, to));
						if(addConstraint(from, to, true))
						{
							UINFO("Added new loop closure between %d and %d.", from, to);
							++added;
							addedLinks.insert(from);
							addedLinks.insert(to);
							lastAdded.first = from;
							lastAdded.second = to;

							progressDialog->appendText(tr("Detected loop closure %1->%2! (%3/%4)").arg(from).arg(to).arg(i+1).arg(clusters.size()));
							QApplication::processEvents();
						}
					}
				}
			}
			progressDialog->incrementStep();
			if(i%100)
			{
				QApplication::processEvents();
			}
		}
		UINFO("Iteration %d/%d: added %d loop closures.", n+1, iterations, (int)addedLinks.size()/2);
		progressDialog->appendText(tr("Iteration %1/%2: Detected %3 loop closures!").arg(n+1).arg(iterations).arg(addedLinks.size()/2));
		if(addedLinks.size() == 0)
		{
			break;
		}
	}

	odomMaxInf_.clear();

	if(added)
	{
		this->updateGraphView();
		this->updateLoopClosuresSlider(lastAdded.first, lastAdded.second);
	}
	UINFO("Total added %d loop closures.", added);

	progressDialog->appendText(tr("Total new loop closures detected=%1").arg(added));
	progressDialog->setValue(progressDialog->maximumSteps());
}

void DatabaseViewer::updateAllNeighborCovariances()
{
	updateAllCovariances(neighborLinks_);
}
void DatabaseViewer::updateAllLoopClosureCovariances()
{
	updateAllCovariances(loopLinks_);
}

void DatabaseViewer::updateAllCovariances(const QList<Link> & links)
{
	if(links.size())
	{
		bool ok = false;
		double stddev = QInputDialog::getDouble(this, tr("Linear error"), tr("Std deviation (m)"), 0.01, 0.0001, 9, 4, &ok);
		if(!ok) return;
		double linearVar = stddev*stddev;
		stddev = QInputDialog::getDouble(this, tr("Angular error"), tr("Std deviation (deg)"), 1, 0.01, 45, 2, &ok)*M_PI/180.0;
		if(!ok) return;
		double angularVar = stddev*stddev;

		rtabmap::ProgressDialog * progressDialog = new rtabmap::ProgressDialog(this);
		progressDialog->setAttribute(Qt::WA_DeleteOnClose);
		progressDialog->setMaximumSteps(links.size());
		progressDialog->setCancelButtonVisible(true);
		progressDialog->setMinimumWidth(800);
		progressDialog->show();

		cv::Mat infMatrix = cv::Mat::eye(6,6,CV_64FC1);
		infMatrix(cv::Range(0,3), cv::Range(0,3))/=linearVar;
		infMatrix(cv::Range(3,6), cv::Range(3,6))/=angularVar;

		for(int i=0; i<links.size(); ++i)
		{
			int from = links[i].from();
			int to = links[i].to();

			Link currentLink =  findActiveLink(from, to);
			if(!currentLink.isValid())
			{
				UERROR("Not found link! (%d->%d)", from, to);
				return;
			}
			currentLink = Link(
					currentLink.from(),
					currentLink.to(),
					currentLink.type(),
					currentLink.transform(),
					infMatrix.clone(),
					currentLink.userDataCompressed());
			bool updated = false;
			std::multimap<int, Link>::iterator iter = linksRefined_.find(currentLink.from());
			while(iter != linksRefined_.end() && iter->first == currentLink.from())
			{
				if(iter->second.to() == currentLink.to() &&
				   iter->second.type() == currentLink.type())
				{
					iter->second = currentLink;
					updated = true;
					break;
				}
				++iter;
			}
			if(!updated)
			{
				linksRefined_.insert(std::make_pair(currentLink.from(), currentLink));
			}

			progressDialog->appendText(tr("Updated link %1->%2 (%3/%4)").arg(from).arg(to).arg(i+1).arg(links.size()));
			progressDialog->incrementStep();
			QApplication::processEvents();
			if(progressDialog->isCanceled())
			{
				break;
			}
		}
		this->updateGraphView();

		progressDialog->setValue(progressDialog->maximumSteps());
		progressDialog->appendText("Refining links finished!");
	}
}

void DatabaseViewer::refineAllNeighborLinks()
{
	refineAllLinks(neighborLinks_);
}
void DatabaseViewer::refineAllLoopClosureLinks()
{
	refineAllLinks(loopLinks_);
}
void DatabaseViewer::refineAllLinks(const QList<Link> & links)
{
	if(links.size())
	{
		rtabmap::ProgressDialog * progressDialog = new rtabmap::ProgressDialog(this);
		progressDialog->setAttribute(Qt::WA_DeleteOnClose);
		progressDialog->setMaximumSteps(links.size());
		progressDialog->setCancelButtonVisible(true);
		progressDialog->setMinimumWidth(800);
		progressDialog->show();

		for(int i=0; i<links.size(); ++i)
		{
			int from = links[i].from();
			int to = links[i].to();
			this->refineConstraint(links[i].from(), links[i].to(), true);

			progressDialog->appendText(tr("Refined link %1->%2 (%3/%4)").arg(from).arg(to).arg(i+1).arg(links.size()));
			progressDialog->incrementStep();
			QApplication::processEvents();
			if(progressDialog->isCanceled())
			{
				break;
			}
		}
		this->updateGraphView();

		progressDialog->setValue(progressDialog->maximumSteps());
		progressDialog->appendText("Refining links finished!");
	}
}

void DatabaseViewer::resetAllChanges()
{
	linksAdded_.clear();
	linksRefined_.clear();
	linksRemoved_.clear();
	generatedLocalMaps_.clear();
	generatedLocalMapsInfo_.clear();
	updateLoopClosuresSlider();
	this->updateGraphView();
}

void DatabaseViewer::sliderAValueChanged(int value)
{
	this->update(value,
			ui_->label_indexA,
			ui_->label_parentsA,
			ui_->label_childrenA,
			ui_->label_weightA,
			ui_->label_labelA,
			ui_->label_stampA,
			ui_->graphicsView_A,
			ui_->label_idA,
			ui_->label_mapA,
			ui_->label_poseA,
			ui_->label_velA,
			ui_->label_calibA,
			ui_->label_scanA,
			ui_->label_gravityA,
			ui_->label_gpsA,
			ui_->label_sensorsA,
			true);
}

void DatabaseViewer::sliderBValueChanged(int value)
{
	this->update(value,
			ui_->label_indexB,
			ui_->label_parentsB,
			ui_->label_childrenB,
			ui_->label_weightB,
			ui_->label_labelB,
			ui_->label_stampB,
			ui_->graphicsView_B,
			ui_->label_idB,
			ui_->label_mapB,
			ui_->label_poseB,
			ui_->label_velB,
			ui_->label_calibB,
			ui_->label_scanB,
			ui_->label_gravityB,
			ui_->label_gpsB,
			ui_->label_sensorsB,
			true);
}

void DatabaseViewer::update(int value,
						QLabel * labelIndex,
						QLabel * labelParents,
						QLabel * labelChildren,
						QLabel * weight,
						QLabel * label,
						QLabel * stamp,
						rtabmap::ImageView * view,
						QLabel * labelId,
						QLabel * labelMapId,
						QLabel * labelPose,
						QLabel * labelVelocity,
						QLabel * labelCalib,
						QLabel * labelScan,
						QLabel * labelGravity,
						QLabel * labelGps,
						QLabel * labelSensors,
						bool updateConstraintView)
{
	lastSliderIndexBrowsed_ = value;

	UTimer timer;
	labelIndex->setText(QString::number(value));
	labelParents->clear();
	labelChildren->clear();
	weight->clear();
	label->clear();
	labelMapId->clear();
	labelPose->clear();
	labelVelocity->clear();
	stamp->clear();
	labelCalib->clear();
	labelScan ->clear();
	labelGravity->clear();
	labelGps->clear();
	labelSensors->clear();
	QRectF rect;
	if(value >= 0 && value < ids_.size())
	{
		view->clear();
		int id = ids_.at(value);
		int mapId = -1;
		labelId->setText(QString::number(id));
		if(id>0)
		{
			//image
			QImage img;
			cv::Mat imgDepth;
			if(dbDriver_)
			{
				SensorData data;
				dbDriver_->getNodeData(id, data);
				data.uncompressData();
				if(!data.imageRaw().empty())
				{
					img = uCvMat2QImage(ui_->label_indexB==labelIndex?data.imageRaw():data.imageRaw());
				}
				if(!data.depthOrRightRaw().empty())
				{
					cv::Mat depth =data.depthOrRightRaw();
					if(!data.depthRaw().empty())
					{
						if(ui_->spinBox_mesh_fillDepthHoles->value() > 0)
						{
							depth = util2d::fillDepthHoles(depth, ui_->spinBox_mesh_fillDepthHoles->value(), float(ui_->spinBox_mesh_depthError->value())/100.0f);
						}
					}
					imgDepth = depth;
				}

				std::list<int> ids;
				ids.push_back(id);
				std::list<Signature*> signatures;
				dbDriver_->loadSignatures(ids, signatures);

				if(signatures.size() && signatures.front()!=0 && signatures.front()->getWords().size())
				{
					view->setFeatures(signatures.front()->getWords(), data.depthOrRightRaw().type() == CV_8UC1?cv::Mat():data.depthOrRightRaw(), Qt::yellow);
				}

				Transform odomPose, g;
				int w;
				std::string l;
				double s;
				std::vector<float> v;
				GPS gps;
				EnvSensors sensors;
				dbDriver_->getNodeInfo(id, odomPose, mapId, w, l, s, g, v, gps, sensors);

				weight->setNum(w);
				label->setText(l.c_str());
				float x,y,z,roll,pitch,yaw;
				odomPose.getTranslationAndEulerAngles(x,y,z,roll, pitch,yaw);
				labelPose->setText(QString("%1xyz=(%2,%3,%4)\nrpy=(%5,%6,%7)").arg(odomPose.isIdentity()?"* ":"").arg(x).arg(y).arg(z).arg(roll).arg(pitch).arg(yaw));
				if(odomPoses_.size() && odomPoses_.find(id) == odomPoses_.end())
				{
					labelPose->setText(labelPose->text() + "\n<Not in graph>");
				}
				if(s!=0.0)
				{
					stamp->setText(QString::number(s, 'f'));
					stamp->setToolTip(QDateTime::fromMSecsSinceEpoch(s*1000.0).toString("dd.MM.yyyy hh:mm:ss.zzz"));
				}
				if(v.size()==6)
				{
					labelVelocity->setText(QString("vx=%1 vy=%2 vz=%3 vroll=%4 vpitch=%5 vyaw=%6").arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3]).arg(v[4]).arg(v[5]));
				}
				std::multimap<int, Link>::const_iterator imuIter = graph::findLink(links_, id, id, false, Link::kGravity);
				if(imuIter != links_.end())
				{
					float roll,pitch,yaw;
					imuIter->second.transform().getEulerAngles(roll, pitch, yaw);
					Eigen::Vector3d v = Transform(0,0,0,roll,pitch,0).toEigen3d() * -Eigen::Vector3d::UnitZ();
					labelGravity->setText(QString("x=%1 y=%2 z=%3").arg(v[0]).arg(v[1]).arg(v[2]));
				}
				if(gps.stamp()>0.0)
				{
					labelGps->setText(QString("stamp=%1 longitude=%2 latitude=%3 altitude=%4m error=%5m bearing=%6deg").arg(QString::number(gps.stamp(), 'f')).arg(gps.longitude()).arg(gps.latitude()).arg(gps.altitude()).arg(gps.error()).arg(gps.bearing()));
					labelGps->setToolTip(QDateTime::fromMSecsSinceEpoch(gps.stamp()*1000.0).toString("dd.MM.yyyy hh:mm:ss.zzz"));
				}
				if(sensors.size())
				{
					QString sensorsStr;
					QString tooltipStr;
					for(EnvSensors::iterator iter=sensors.begin(); iter!=sensors.end(); ++iter)
					{
						if(iter != sensors.begin())
						{
							sensorsStr += " | ";
							tooltipStr += " | ";
						}

						if(iter->first == EnvSensor::kWifiSignalStrength)
						{
							sensorsStr += uFormat("%.1f dbm", iter->second.value()).c_str();
							tooltipStr += "Wifi signal strength";
						}
						else if(iter->first == EnvSensor::kAmbientTemperature)
						{
							sensorsStr += uFormat("%.1f \u00B0C", iter->second.value()).c_str();
							tooltipStr += "Ambient Temperature";
						}
						else if(iter->first == EnvSensor::kAmbientAirPressure)
						{
							sensorsStr += uFormat("%.1f hPa", iter->second.value()).c_str();
							tooltipStr += "Ambient Air Pressure";
						}
						else if(iter->first == EnvSensor::kAmbientLight)
						{
							sensorsStr += uFormat("%.0f lx", iter->second.value()).c_str();
							tooltipStr += "Ambient Light";
						}
						else if(iter->first == EnvSensor::kAmbientRelativeHumidity)
						{
							sensorsStr += uFormat("%.0f %%", iter->second.value()).c_str();
							tooltipStr += "Ambient Relative Humidity";
						}
						else
						{
							sensorsStr += uFormat("%.2f", iter->second.value()).c_str();
							tooltipStr += QString("Type %1").arg((int)iter->first);
						}

					}
					labelSensors->setText(sensorsStr);
					labelSensors->setToolTip(tooltipStr);
				}
				if(data.cameraModels().size() || data.stereoCameraModel().isValidForProjection())
				{
					std::stringstream calibrationDetails;
					if(data.cameraModels().size())
					{
						if(!data.depthRaw().empty() && data.depthRaw().cols!=data.imageRaw().cols && data.imageRaw().cols)
						{
							labelCalib->setText(tr("%1 %2x%3 [%8x%9] fx=%4 fy=%5 cx=%6 cy=%7 T=%10")
									.arg(data.cameraModels().size())
									.arg(data.cameraModels()[0].imageWidth()>0?data.cameraModels()[0].imageWidth():data.imageRaw().cols/data.cameraModels().size())
									.arg(data.cameraModels()[0].imageHeight()>0?data.cameraModels()[0].imageHeight():data.imageRaw().rows)
									.arg(data.cameraModels()[0].fx())
									.arg(data.cameraModels()[0].fy())
									.arg(data.cameraModels()[0].cx())
									.arg(data.cameraModels()[0].cy())
									.arg(data.depthRaw().cols/data.cameraModels().size())
									.arg(data.depthRaw().rows)
									.arg(data.cameraModels()[0].localTransform().prettyPrint().c_str()));
						}
						else
						{
							labelCalib->setText(tr("%1 %2x%3 fx=%4 fy=%5 cx=%6 cy=%7 T=%8")
									.arg(data.cameraModels().size())
									.arg(data.cameraModels()[0].imageWidth()>0?data.cameraModels()[0].imageWidth():data.imageRaw().cols/data.cameraModels().size())
									.arg(data.cameraModels()[0].imageHeight()>0?data.cameraModels()[0].imageHeight():data.imageRaw().rows)
									.arg(data.cameraModels()[0].fx())
									.arg(data.cameraModels()[0].fy())
									.arg(data.cameraModels()[0].cx())
									.arg(data.cameraModels()[0].cy())
									.arg(data.cameraModels()[0].localTransform().prettyPrint().c_str()));
						}

						for(unsigned int i=0; i<data.cameraModels().size();++i)
						{
							if(i!=0) calibrationDetails << std::endl;
							calibrationDetails << "Id: " << i << " Size=" << data.cameraModels()[i].imageWidth() << "x" << data.cameraModels()[i].imageWidth() << std::endl;
							if( data.cameraModels()[i].K_raw().total()) calibrationDetails << "K=" << data.cameraModels()[i].K_raw() << std::endl;
							if( data.cameraModels()[i].D_raw().total()) calibrationDetails << "D=" << data.cameraModels()[i].D_raw() << std::endl;
							if( data.cameraModels()[i].R().total()) calibrationDetails << "R=" << data.cameraModels()[i].R() << std::endl;
							if( data.cameraModels()[i].P().total()) calibrationDetails << "P=" << data.cameraModels()[i].P() << std::endl;
						}

					}
					else
					{
						//stereo
						labelCalib->setText(tr("%1x%2 fx=%3 fy=%4 cx=%5 cy=%6 baseline=%7m T=%8")
									.arg(data.stereoCameraModel().left().imageWidth()>0?data.stereoCameraModel().left().imageWidth():data.imageRaw().cols)
									.arg(data.stereoCameraModel().left().imageHeight()>0?data.stereoCameraModel().left().imageHeight():data.imageRaw().rows)
									.arg(data.stereoCameraModel().left().fx())
									.arg(data.stereoCameraModel().left().fy())
									.arg(data.stereoCameraModel().left().cx())
									.arg(data.stereoCameraModel().left().cy())
									.arg(data.stereoCameraModel().baseline())
									.arg(data.stereoCameraModel().localTransform().prettyPrint().c_str()));

						calibrationDetails << "Left:" << " Size=" << data.stereoCameraModel().left().imageWidth() << "x" << data.stereoCameraModel().left().imageWidth() << std::endl;
						if( data.stereoCameraModel().left().K_raw().total()) calibrationDetails << "K=" << data.stereoCameraModel().left().K_raw() << std::endl;
						if( data.stereoCameraModel().left().D_raw().total()) calibrationDetails << "D=" << data.stereoCameraModel().left().D_raw() << std::endl;
						if( data.stereoCameraModel().left().R().total()) calibrationDetails << "R=" << data.stereoCameraModel().left().R() << std::endl;
						if( data.stereoCameraModel().left().P().total()) calibrationDetails << "P=" << data.stereoCameraModel().left().P() << std::endl;
						calibrationDetails << std::endl;
						calibrationDetails << "Right:" << " Size=" << data.stereoCameraModel().right().imageWidth() << "x" << data.stereoCameraModel().right().imageWidth() << std::endl;
						if( data.stereoCameraModel().right().K_raw().total()) calibrationDetails << "K=" << data.stereoCameraModel().right().K_raw() << std::endl;
						if( data.stereoCameraModel().right().D_raw().total()) calibrationDetails << "D=" << data.stereoCameraModel().right().D_raw() << std::endl;
						if( data.stereoCameraModel().right().R().total()) calibrationDetails << "R=" << data.stereoCameraModel().right().R() << std::endl;
						if( data.stereoCameraModel().right().P().total()) calibrationDetails << "P=" << data.stereoCameraModel().right().P() << std::endl;
						calibrationDetails << std::endl;
						if( data.stereoCameraModel().R().total()) calibrationDetails << "R=" << data.stereoCameraModel().R() << std::endl;
						if( data.stereoCameraModel().T().total()) calibrationDetails << "T=" << data.stereoCameraModel().T() << std::endl;
						if( data.stereoCameraModel().F().total()) calibrationDetails << "F=" << data.stereoCameraModel().F() << std::endl;
						if( data.stereoCameraModel().E().total()) calibrationDetails << "E=" << data.stereoCameraModel().E() << std::endl;
					}
					labelCalib->setToolTip(calibrationDetails.str().c_str());

				}
				else
				{
					labelCalib->setText("NA");
				}

				if(data.laserScanRaw().size())
				{
					labelScan->setText(tr("Format=%1 Points=%2 [max=%3] Range=[%4->%5 m] Angle=[%6->%7 rad inc=%8] Has [Color=%9 2D=%10 Normals=%11 Intensity=%12]")
							.arg(data.laserScanRaw().format())
							.arg(data.laserScanRaw().size())
							.arg(data.laserScanRaw().maxPoints())
							.arg(data.laserScanRaw().rangeMin())
							.arg(data.laserScanRaw().rangeMax())
							.arg(data.laserScanRaw().angleMin())
							.arg(data.laserScanRaw().angleMax())
							.arg(data.laserScanRaw().angleIncrement())
							.arg(data.laserScanRaw().hasRGB()?1:0)
							.arg(data.laserScanRaw().is2d()?1:0)
							.arg(data.laserScanRaw().hasNormals()?1:0)
							.arg(data.laserScanRaw().hasIntensity()?1:0));
				}

				//stereo
				if(!data.depthOrRightRaw().empty() && data.depthOrRightRaw().type() == CV_8UC1)
				{
					this->updateStereo(&data);
				}
				else
				{
					stereoViewer_->clear();
					ui_->graphicsView_stereo->clear();
				}

				// 3d view
				if(cloudViewer_->isVisible())
				{
					Transform pose = Transform::getIdentity();
					if(signatures.size() && ui_->checkBox_odomFrame_3dview->isChecked())
					{
						float x, y, z, roll, pitch, yaw;
						(*signatures.begin())->getPose().getTranslationAndEulerAngles(x, y, z, roll, pitch, yaw);
						pose = Transform(0,0,z,roll,pitch,0);
					}

					cloudViewer_->removeAllFrustums();
					cloudViewer_->removeCloud("mesh");
					cloudViewer_->removeCloud("cloud");
					cloudViewer_->removeCloud("scan");
					cloudViewer_->removeCloud("map");
					cloudViewer_->removeCloud("ground");
					cloudViewer_->removeCloud("obstacles");
					cloudViewer_->removeCloud("empty_cells");
					cloudViewer_->removeCloud("words");
					cloudViewer_->removeOctomap();
					if(ui_->checkBox_showCloud->isChecked() || ui_->checkBox_showMesh->isChecked())
					{
						if(!data.depthOrRightRaw().empty())
						{
							if(!data.imageRaw().empty())
							{
								pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
								pcl::IndicesPtr indices(new std::vector<int>);
								if(!data.depthRaw().empty() && data.cameraModels().size()==1)
								{
									cv::Mat depth = data.depthRaw();
									if(ui_->spinBox_mesh_fillDepthHoles->value() > 0)
									{
										depth = util2d::fillDepthHoles(depth, ui_->spinBox_mesh_fillDepthHoles->value(), float(ui_->spinBox_mesh_depthError->value())/100.0f);
									}
									cloud = util3d::cloudFromDepthRGB(
											data.imageRaw(),
											depth,
											data.cameraModels()[0],
											ui_->spinBox_decimation->value(),0,0,indices.get());
									if(indices->size())
									{
										cloud = util3d::transformPointCloud(cloud, data.cameraModels()[0].localTransform());
									}

								}
								else
								{
									cloud = util3d::cloudRGBFromSensorData(data, ui_->spinBox_decimation->value(), 0, 0, indices.get(), ui_->parameters_toolbox->getParameters());
								}
								if(indices->size())
								{
									if(ui_->doubleSpinBox_voxelSize->value() > 0.0)
									{
										cloud = util3d::voxelize(cloud, indices, ui_->doubleSpinBox_voxelSize->value());
									}

									if(ui_->checkBox_showMesh->isChecked() && !cloud->is_dense)
									{
										Eigen::Vector3f viewpoint(0.0f,0.0f,0.0f);
										if(data.cameraModels().size() && !data.cameraModels()[0].localTransform().isNull())
										{
											viewpoint[0] = data.cameraModels()[0].localTransform().x();
											viewpoint[1] = data.cameraModels()[0].localTransform().y();
											viewpoint[2] = data.cameraModels()[0].localTransform().z();
										}
										else if(!data.stereoCameraModel().localTransform().isNull())
										{
											viewpoint[0] = data.stereoCameraModel().localTransform().x();
											viewpoint[1] = data.stereoCameraModel().localTransform().y();
											viewpoint[2] = data.stereoCameraModel().localTransform().z();
										}
										std::vector<pcl::Vertices> polygons = util3d::organizedFastMesh(
												cloud,
												float(ui_->spinBox_mesh_angleTolerance->value())*M_PI/180.0f,
												ui_->checkBox_mesh_quad->isChecked(),
												ui_->spinBox_mesh_triangleSize->value(),
												viewpoint);

										if(ui_->spinBox_mesh_minClusterSize->value())
										{
											// filter polygons
											std::vector<std::set<int> > neighbors;
											std::vector<std::set<int> > vertexToPolygons;
											util3d::createPolygonIndexes(polygons,
													cloud->size(),
													neighbors,
													vertexToPolygons);
											std::list<std::list<int> > clusters = util3d::clusterPolygons(
													neighbors,
													ui_->spinBox_mesh_minClusterSize->value());
											std::vector<pcl::Vertices> filteredPolygons(polygons.size());
											int oi=0;
											for(std::list<std::list<int> >::iterator iter=clusters.begin(); iter!=clusters.end(); ++iter)
											{
												for(std::list<int>::iterator jter=iter->begin(); jter!=iter->end(); ++jter)
												{
													filteredPolygons[oi++] = polygons.at(*jter);
												}
											}
											filteredPolygons.resize(oi);
											polygons = filteredPolygons;
										}

										cloudViewer_->addCloudMesh("mesh", cloud, polygons, pose);
									}
									if(ui_->checkBox_showCloud->isChecked())
									{
										cloudViewer_->addCloud("cloud", cloud, pose);
									}
								}
							}
							else if(ui_->checkBox_showCloud->isChecked())
							{
								pcl::PointCloud<pcl::PointXYZ>::Ptr cloud;
								pcl::IndicesPtr indices(new std::vector<int>);
								cloud = util3d::cloudFromSensorData(data, ui_->spinBox_decimation->value(), 0, 0, indices.get(), ui_->parameters_toolbox->getParameters());
								if(indices->size())
								{
									if(ui_->doubleSpinBox_voxelSize->value() > 0.0)
									{
										cloud = util3d::voxelize(cloud, indices, ui_->doubleSpinBox_voxelSize->value());
									}

									cloudViewer_->addCloud("cloud", cloud, pose);
								}
							}
						}
					}

					//frustums
					if(cloudViewer_->isFrustumShown())
					{
						if(data.cameraModels().size())
						{
							cloudViewer_->updateCameraFrustums(pose, data.cameraModels());
						}
						else
						{
							cloudViewer_->updateCameraFrustum(pose, data.stereoCameraModel());
						}
					}

					//words
					if(ui_->checkBox_showWords->isChecked() && signatures.size())
					{
						pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
						cloud->resize((*signatures.begin())->getWords3().size());
						int i=0;
						for(std::multimap<int, cv::Point3f>::const_iterator iter=(*signatures.begin())->getWords3().begin();
							iter!=(*signatures.begin())->getWords3().end();
							++iter)
						{
							cloud->at(i++) = pcl::PointXYZ(iter->second.x, iter->second.y, iter->second.z);
						}

						if(cloud->size())
						{
							cloud = rtabmap::util3d::removeNaNFromPointCloud(cloud);
						}

						if(cloud->size())
						{
							cloudViewer_->addCloud("words", cloud, pose, Qt::red);
						}
					}

					//add scan
					if(ui_->checkBox_showScan->isChecked() && data.laserScanRaw().size())
					{
						if(data.laserScanRaw().hasRGB() && data.laserScanRaw().hasNormals())
						{
							pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr scan = util3d::laserScanToPointCloudRGBNormal(data.laserScanRaw(), data.laserScanRaw().localTransform());
							cloudViewer_->addCloud("scan", scan, pose, Qt::yellow);
						}
						else if(data.laserScanRaw().hasIntensity() && data.laserScanRaw().hasNormals())
						{
							pcl::PointCloud<pcl::PointXYZINormal>::Ptr scan = util3d::laserScanToPointCloudINormal(data.laserScanRaw(), data.laserScanRaw().localTransform());
							cloudViewer_->addCloud("scan", scan, pose, Qt::yellow);
						}
						else if(data.laserScanRaw().hasNormals())
						{
							pcl::PointCloud<pcl::PointNormal>::Ptr scan = util3d::laserScanToPointCloudNormal(data.laserScanRaw(), data.laserScanRaw().localTransform());
							cloudViewer_->addCloud("scan", scan, pose, Qt::yellow);
						}
						else if(data.laserScanRaw().hasRGB())
						{
							pcl::PointCloud<pcl::PointXYZRGB>::Ptr scan = util3d::laserScanToPointCloudRGB(data.laserScanRaw(), data.laserScanRaw().localTransform());
							cloudViewer_->addCloud("scan", scan, pose, Qt::yellow);
						}
						else
						{
							pcl::PointCloud<pcl::PointXYZ>::Ptr scan = util3d::laserScanToPointCloud(data.laserScanRaw(), data.laserScanRaw().localTransform());
							cloudViewer_->addCloud("scan", scan, pose, Qt::yellow);
						}
					}

					//add occupancy grid
					if(ui_->checkBox_showMap->isChecked() || ui_->checkBox_showGrid->isChecked())
					{
						std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat> > localMaps;
						std::map<int, std::pair<float, cv::Point3f> > localMapsInfo;
						if(generatedLocalMaps_.find(data.id()) != generatedLocalMaps_.end())
						{
							localMaps.insert(*generatedLocalMaps_.find(data.id()));
							localMapsInfo.insert(*generatedLocalMapsInfo_.find(data.id()));
						}
						else if(!data.gridGroundCellsRaw().empty() || !data.gridObstacleCellsRaw().empty())
						{
							localMaps.insert(std::make_pair(data.id(), std::make_pair(std::make_pair(data.gridGroundCellsRaw(), data.gridObstacleCellsRaw()), data.gridEmptyCellsRaw())));
							localMapsInfo.insert(std::make_pair(data.id(), std::make_pair(data.gridCellSize(), data.gridViewPoint())));
						}
						if(!localMaps.empty())
						{
							std::map<int, Transform> poses;
							poses.insert(std::make_pair(data.id(), pose));

#ifdef RTABMAP_OCTOMAP
							OctoMap * octomap = 0;
							if(ui_->checkBox_octomap->isChecked() &&
								(!localMaps.begin()->second.first.first.empty() || !localMaps.begin()->second.first.second.empty()) &&
								(localMaps.begin()->second.first.first.empty() || localMaps.begin()->second.first.first.channels() > 2) &&
								(localMaps.begin()->second.first.second.empty() || localMaps.begin()->second.first.second.channels() > 2) &&
								(localMaps.begin()->second.second.empty() || localMaps.begin()->second.second.channels() > 2) &&
								localMapsInfo.begin()->second.first > 0.0f)
							{
								//create local octomap
								octomap = new OctoMap(localMapsInfo.begin()->second.first);
								octomap->addToCache(data.id(), localMaps.begin()->second.first.first, localMaps.begin()->second.first.second, localMaps.begin()->second.second, localMapsInfo.begin()->second.second);
								octomap->update(poses);
							}
#endif

							if(ui_->checkBox_showMap->isChecked())
							{
								float xMin=0.0f, yMin=0.0f;
								cv::Mat map8S;
								ParametersMap parameters = ui_->parameters_toolbox->getParameters();
								float gridCellSize = Parameters::defaultGridCellSize();
								Parameters::parse(parameters, Parameters::kGridCellSize(), gridCellSize);
#ifdef RTABMAP_OCTOMAP
								if(octomap)
								{
									map8S = octomap->createProjectionMap(xMin, yMin, gridCellSize, 0);
								}
								else
#endif
								{
									OccupancyGrid grid(ui_->parameters_toolbox->getParameters());
									grid.setCellSize(gridCellSize);
									grid.addToCache(data.id(), localMaps.begin()->second.first.first, localMaps.begin()->second.first.second, localMaps.begin()->second.second);
									grid.update(poses);
									map8S = grid.getMap(xMin, yMin);
								}
								if(!map8S.empty())
								{
									//convert to gray scaled map
									cloudViewer_->addOccupancyGridMap(util3d::convertMap2Image8U(map8S), gridCellSize, xMin, yMin, 1);
								}
							}

							if(ui_->checkBox_showGrid->isChecked())
							{
#ifdef RTABMAP_OCTOMAP
								if(octomap)
								{
									if(ui_->comboBox_octomap_rendering_type->currentIndex()== 0)
									{
										pcl::IndicesPtr obstacles(new std::vector<int>);
										pcl::IndicesPtr empty(new std::vector<int>);
										pcl::IndicesPtr ground(new std::vector<int>);
										pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud = octomap->createCloud(ui_->spinBox_grid_depth->value(), obstacles.get(), empty.get(), ground.get());
										if(octomap->hasColor())
										{
											pcl::PointCloud<pcl::PointXYZRGB>::Ptr obstaclesCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
											pcl::copyPointCloud(*cloud, *obstacles, *obstaclesCloud);
											cloudViewer_->addCloud("obstacles", obstaclesCloud, Transform::getIdentity(), QColor(ui_->lineEdit_obstacleColor->text()));
											cloudViewer_->setCloudPointSize("obstacles", 5);

											pcl::PointCloud<pcl::PointXYZRGB>::Ptr groundCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
											pcl::copyPointCloud(*cloud, *ground, *groundCloud);
											cloudViewer_->addCloud("ground", groundCloud, Transform::getIdentity(), QColor(ui_->lineEdit_groundColor->text()));
											cloudViewer_->setCloudPointSize("ground", 5);
										}
										else
										{
											pcl::PointCloud<pcl::PointXYZ>::Ptr obstaclesCloud(new pcl::PointCloud<pcl::PointXYZ>);
											pcl::copyPointCloud(*cloud, *obstacles, *obstaclesCloud);
											cloudViewer_->addCloud("obstacles", obstaclesCloud, Transform::getIdentity(), QColor(ui_->lineEdit_obstacleColor->text()));
											cloudViewer_->setCloudPointSize("obstacles", 5);

											pcl::PointCloud<pcl::PointXYZ>::Ptr groundCloud(new pcl::PointCloud<pcl::PointXYZ>);
											pcl::copyPointCloud(*cloud, *ground, *groundCloud);
											cloudViewer_->addCloud("ground", groundCloud, Transform::getIdentity(), QColor(ui_->lineEdit_groundColor->text()));
											cloudViewer_->setCloudPointSize("ground", 5);
										}

										if(ui_->checkBox_grid_empty->isChecked())
										{
											pcl::PointCloud<pcl::PointXYZ>::Ptr emptyCloud(new pcl::PointCloud<pcl::PointXYZ>);
											pcl::copyPointCloud(*cloud, *empty, *emptyCloud);
											cloudViewer_->addCloud("empty_cells", emptyCloud, Transform::getIdentity(), QColor(ui_->lineEdit_emptyColor->text()));
											cloudViewer_->setCloudOpacity("empty_cells", 0.5);
											cloudViewer_->setCloudPointSize("empty_cells", 5);
										}
									}
									else
									{
										cloudViewer_->addOctomap(octomap, ui_->spinBox_grid_depth->value(), ui_->comboBox_octomap_rendering_type->currentIndex()>1);
									}
								}
								else
#endif
								{
									// occupancy cloud
									LaserScan scan = LaserScan::backwardCompatibility(localMaps.begin()->second.first.first);
									if(scan.hasRGB())
									{
										cloudViewer_->addCloud("ground", util3d::laserScanToPointCloudRGB(scan), pose, QColor(ui_->lineEdit_groundColor->text()));
									}
									else
									{
										cloudViewer_->addCloud("ground", util3d::laserScanToPointCloud(scan), pose, QColor(ui_->lineEdit_groundColor->text()));
									}
									scan = LaserScan::backwardCompatibility(localMaps.begin()->second.first.second);
									if(scan.hasRGB())
									{
										cloudViewer_->addCloud("obstacles", util3d::laserScanToPointCloudRGB(scan), pose, QColor(ui_->lineEdit_obstacleColor->text()));
									}
									else
									{
										cloudViewer_->addCloud("obstacles", util3d::laserScanToPointCloud(scan), pose, QColor(ui_->lineEdit_obstacleColor->text()));
									}

									cloudViewer_->setCloudPointSize("ground", 5);
									cloudViewer_->setCloudPointSize("obstacles", 5);

									if(ui_->checkBox_grid_empty->isChecked())
									{
										cloudViewer_->addCloud("empty_cells",
												util3d::laserScanToPointCloud(LaserScan::backwardCompatibility(localMaps.begin()->second.second)),
												pose,
												QColor(ui_->lineEdit_emptyColor->text()));
										cloudViewer_->setCloudPointSize("empty_cells", 5);
										cloudViewer_->setCloudOpacity("empty_cells", 0.5);
									}
								}
							}
#ifdef RTABMAP_OCTOMAP
							if(octomap)
							{
								delete octomap;
							}
#endif
						}
					}
					cloudViewer_->update();
				}

				if(signatures.size())
				{
					UASSERT(signatures.front() != 0 && signatures.size() == 1);
					delete signatures.front();
					signatures.clear();
				}
			}

			if(!img.isNull())
			{
				view->setImage(img);
				rect = img.rect();
			}
			else
			{
				ULOGGER_DEBUG("Image is empty");
			}

			if(!imgDepth.empty())
			{
				view->setImageDepth(imgDepth);
				if(img.isNull())
				{
					rect.setWidth(imgDepth.cols);
					rect.setHeight(imgDepth.rows);
				}
			}
			else
			{
				ULOGGER_DEBUG("Image depth is empty");
			}

			// loops
			std::multimap<int, rtabmap::Link> links;
			dbDriver_->loadLinks(id, links);
			if(links.size())
			{
				QString strParents, strChildren;
				for(std::multimap<int, rtabmap::Link>::iterator iter=links.begin(); iter!=links.end(); ++iter)
				{
					if(iter->second.type() != Link::kNeighbor &&
				       iter->second.type() != Link::kNeighborMerged)
					{
						if(iter->first < id)
						{
							strChildren.append(QString("%1 ").arg(iter->first));
						}
						else
						{
							strParents.append(QString("%1 ").arg(iter->first));
						}
					}
				}
				labelParents->setText(strParents);
				labelChildren->setText(strChildren);
			}
		}

		if(mapId>=0)
		{
			labelMapId->setText(QString::number(mapId));
		}
	}
	else if(value != 0)
	{
		ULOGGER_ERROR("Slider index out of range ?");
	}

	updateConstraintButtons();
	updateWordsMatching();

	if(updateConstraintView && ui_->dockWidget_constraints->isVisible())
	{
		// update constraint view
		int from = ids_.at(ui_->horizontalSlider_A->value());
		int to = ids_.at(ui_->horizontalSlider_B->value());
		bool set = false;
		for(int i=0; i<loopLinks_.size() || i<neighborLinks_.size(); ++i)
		{
			if(i < loopLinks_.size())
			{
				if((loopLinks_[i].from() == from && loopLinks_[i].to() == to) ||
				   (loopLinks_[i].from() == to && loopLinks_[i].to() == from))
				{
					if(i != ui_->horizontalSlider_loops->value())
					{
						ui_->horizontalSlider_loops->blockSignals(true);
						ui_->horizontalSlider_loops->setValue(i);
						ui_->horizontalSlider_loops->blockSignals(false);
						this->updateConstraintView(loopLinks_[i].from() == from?loopLinks_.at(i):loopLinks_.at(i).inverse(), false);
					}
					ui_->horizontalSlider_neighbors->blockSignals(true);
					ui_->horizontalSlider_neighbors->setValue(0);
					ui_->horizontalSlider_neighbors->blockSignals(false);
					set = true;
					break;
				}
			}
			if(i < neighborLinks_.size())
			{
				if((neighborLinks_[i].from() == from && neighborLinks_[i].to() == to) ||
				   (neighborLinks_[i].from() == to && neighborLinks_[i].to() == from))
				{
					if(i != ui_->horizontalSlider_neighbors->value())
					{
						ui_->horizontalSlider_neighbors->blockSignals(true);
						ui_->horizontalSlider_neighbors->setValue(i);
						ui_->horizontalSlider_neighbors->blockSignals(false);
						this->updateConstraintView(neighborLinks_[i].from() == from?neighborLinks_.at(i):neighborLinks_.at(i).inverse(), false);
					}
					ui_->horizontalSlider_loops->blockSignals(true);
					ui_->horizontalSlider_loops->setValue(0);
					ui_->horizontalSlider_loops->blockSignals(false);
					set = true;
					break;
				}
			}
		}
		if(!set)
		{
			ui_->horizontalSlider_loops->blockSignals(true);
			ui_->horizontalSlider_neighbors->blockSignals(true);
			ui_->horizontalSlider_loops->setValue(0);
			ui_->horizontalSlider_neighbors->setValue(0);
			ui_->horizontalSlider_loops->blockSignals(false);
			ui_->horizontalSlider_neighbors->blockSignals(false);

			constraintsViewer_->removeAllClouds();

			// make a fake link using globally optimized poses
			if(graphes_.size())
			{
				std::map<int, Transform> optimizedPoses = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());
				if(optimizedPoses.size() > 0)
				{
					std::map<int, Transform>::iterator fromIter = optimizedPoses.find(from);
					std::map<int, Transform>::iterator toIter = optimizedPoses.find(to);
					if(fromIter != optimizedPoses.end() &&
					   toIter != optimizedPoses.end())
					{
						Link link(from, to, Link::kUndef, fromIter->second.inverse() * toIter->second);
						this->updateConstraintView(link, false);
					}
				}
			}

			constraintsViewer_->update();

		}
	}

	if(rect.isValid())
	{
		view->setSceneRect(rect);
	}
}

void DatabaseViewer::updateLoggerLevel()
{
	if(this->parent() == 0)
	{
		ULogger::setLevel((ULogger::Level)ui_->comboBox_logger_level->currentIndex());
	}
}

void DatabaseViewer::updateStereo()
{
	if(ui_->horizontalSlider_A->maximum())
	{
		int id = ids_.at(ui_->horizontalSlider_A->value());
		SensorData data;
		dbDriver_->getNodeData(id, data);
		data.uncompressData();
		updateStereo(&data);
	}
}

void DatabaseViewer::updateStereo(const SensorData * data)
{
	if(data &&
		ui_->dockWidget_stereoView->isVisible() &&
		!data->imageRaw().empty() &&
		!data->depthOrRightRaw().empty() &&
		data->depthOrRightRaw().type() == CV_8UC1 &&
		data->stereoCameraModel().isValidForProjection())
	{
		cv::Mat leftMono;
		if(data->imageRaw().channels() == 3)
		{
			cv::cvtColor(data->imageRaw(), leftMono, CV_BGR2GRAY);
		}
		else
		{
			leftMono = data->imageRaw();
		}

		UTimer timer;
		ParametersMap parameters = ui_->parameters_toolbox->getParameters();
		Stereo * stereo = Stereo::create(parameters);

		// generate kpts
		std::vector<cv::KeyPoint> kpts;
		uInsert(parameters, ParametersPair(Parameters::kKpMaxFeatures(), parameters.at(Parameters::kVisMaxFeatures())));
		uInsert(parameters, ParametersPair(Parameters::kKpMinDepth(), parameters.at(Parameters::kVisMinDepth())));
		uInsert(parameters, ParametersPair(Parameters::kKpMaxDepth(), parameters.at(Parameters::kVisMaxDepth())));
		uInsert(parameters, ParametersPair(Parameters::kKpDetectorStrategy(), parameters.at(Parameters::kVisFeatureType())));
		uInsert(parameters, ParametersPair(Parameters::kKpRoiRatios(), parameters.at(Parameters::kVisRoiRatios())));
		uInsert(parameters, ParametersPair(Parameters::kKpSubPixEps(), parameters.at(Parameters::kVisSubPixEps())));
		uInsert(parameters, ParametersPair(Parameters::kKpSubPixIterations(), parameters.at(Parameters::kVisSubPixIterations())));
		uInsert(parameters, ParametersPair(Parameters::kKpSubPixWinSize(), parameters.at(Parameters::kVisSubPixWinSize())));
		Feature2D * kptDetector = Feature2D::create(parameters);
		kpts = kptDetector->generateKeypoints(leftMono);
		delete kptDetector;

		float timeKpt = timer.ticks();

		std::vector<cv::Point2f> leftCorners;
		cv::KeyPoint::convert(kpts, leftCorners);

		// Find features in the new right image
		std::vector<unsigned char> status;
		std::vector<cv::Point2f> rightCorners;

		rightCorners = stereo->computeCorrespondences(
				leftMono,
				data->rightRaw(),
				leftCorners,
				status);
		delete stereo;

		float timeStereo = timer.ticks();

		pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
		cloud->resize(kpts.size());
		float bad_point = std::numeric_limits<float>::quiet_NaN ();
		UASSERT(status.size() == kpts.size());
		int oi = 0;
		int inliers = 0;
		int flowOutliers= 0;
		int slopeOutliers= 0;
		int negativeDisparityOutliers = 0;
		for(unsigned int i=0; i<status.size(); ++i)
		{
			cv::Point3f pt(bad_point, bad_point, bad_point);
			if(status[i])
			{
				float disparity = leftCorners[i].x - rightCorners[i].x;
				if(disparity > 0.0f)
				{
					cv::Point3f tmpPt = util3d::projectDisparityTo3D(
							leftCorners[i],
							disparity,
							data->stereoCameraModel());

					if(util3d::isFinite(tmpPt))
					{
						pt = util3d::transformPoint(tmpPt, data->stereoCameraModel().left().localTransform());
						status[i] = 100; //blue
						++inliers;
						cloud->at(oi++) = pcl::PointXYZ(pt.x, pt.y, pt.z);
					}
				}
				else
				{
					status[i] = 102; //magenta
					++negativeDisparityOutliers;
				}
			}
			else
			{
				++flowOutliers;
			}
		}
		cloud->resize(oi);

		UINFO("correspondences = %d/%d (%f) (time kpt=%fs stereo=%fs)",
				(int)cloud->size(), (int)leftCorners.size(), float(cloud->size())/float(leftCorners.size()), timeKpt, timeStereo);

		stereoViewer_->updateCameraTargetPosition(Transform::getIdentity());
		stereoViewer_->addCloud("stereo", cloud);
		stereoViewer_->update();

		ui_->label_stereo_inliers->setNum(inliers);
		ui_->label_stereo_flowOutliers->setNum(flowOutliers);
		ui_->label_stereo_slopeOutliers->setNum(slopeOutliers);
		ui_->label_stereo_disparityOutliers->setNum(negativeDisparityOutliers);

		std::vector<cv::KeyPoint> rightKpts;
		cv::KeyPoint::convert(rightCorners, rightKpts);
		std::vector<cv::DMatch> good_matches(kpts.size());
		for(unsigned int i=0; i<good_matches.size(); ++i)
		{
			good_matches[i].trainIdx = i;
			good_matches[i].queryIdx = i;
		}


		//
		//cv::Mat imageMatches;
		//cv::drawMatches( leftMono, kpts, data->getDepthRaw(), rightKpts,
		//			   good_matches, imageMatches, cv::Scalar::all(-1), cv::Scalar::all(-1),
		//			   std::vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS );

		//ui_->graphicsView_stereo->setImage(uCvMat2QImage(imageMatches));

		ui_->graphicsView_stereo->clear();
		ui_->graphicsView_stereo->setLinesShown(true);
		ui_->graphicsView_stereo->setFeaturesShown(false);
		ui_->graphicsView_stereo->setImageDepthShown(true);

		ui_->graphicsView_stereo->setImage(uCvMat2QImage(data->imageRaw()));
		ui_->graphicsView_stereo->setImageDepth(data->depthOrRightRaw());

		// Draw lines between corresponding features...
		for(unsigned int i=0; i<kpts.size(); ++i)
		{
			if(rightKpts[i].pt.x > 0 && rightKpts[i].pt.y > 0)
			{
				QColor c = Qt::green;
				if(status[i] == 0)
				{
					c = Qt::red;
				}
				else if(status[i] == 100)
				{
					c = Qt::blue;
				}
				else if(status[i] == 101)
				{
					c = Qt::yellow;
				}
				else if(status[i] == 102)
				{
					c = Qt::magenta;
				}
				else if(status[i] == 110)
				{
					c = Qt::cyan;
				}
				ui_->graphicsView_stereo->addLine(
						kpts[i].pt.x,
						kpts[i].pt.y,
						rightKpts[i].pt.x,
						rightKpts[i].pt.y,
						c,
						QString("%1: (%2,%3) -> (%4,%5) d=%6").arg(i).arg(kpts[i].pt.x).arg(kpts[i].pt.y).arg(rightKpts[i].pt.x).arg(rightKpts[i].pt.y).arg(kpts[i].pt.x - rightKpts[i].pt.x));
			}
		}
		ui_->graphicsView_stereo->update();
	}
}

void DatabaseViewer::updateWordsMatching()
{
	int from = ids_.at(ui_->horizontalSlider_A->value());
	int to = ids_.at(ui_->horizontalSlider_B->value());
	if(from && to)
	{
		ui_->graphicsView_A->clearLines();
		ui_->graphicsView_A->setFeaturesColor(ui_->graphicsView_A->getDefaultFeatureColor());
		ui_->graphicsView_B->clearLines();
		ui_->graphicsView_B->setFeaturesColor(ui_->graphicsView_B->getDefaultFeatureColor());

		const QMultiMap<int, KeypointItem*> & wordsA = ui_->graphicsView_A->getFeatures();
		const QMultiMap<int, KeypointItem*> & wordsB = ui_->graphicsView_B->getFeatures();
		if(wordsA.size() && wordsB.size())
		{
			QList<int> ids =  wordsA.uniqueKeys();
			for(int i=0; i<ids.size(); ++i)
			{
				if(ids[i] > 0 && wordsA.count(ids[i]) == 1 && wordsB.count(ids[i]) == 1)
				{
					// PINK features
					ui_->graphicsView_A->setFeatureColor(ids[i], ui_->graphicsView_A->getDefaultMatchingFeatureColor());
					ui_->graphicsView_B->setFeatureColor(ids[i], ui_->graphicsView_B->getDefaultMatchingFeatureColor());

					// Add lines
					// Draw lines between corresponding features...
					float scaleX = ui_->graphicsView_A->viewScale();
					float deltaX = 0;
					float deltaY = 0;

					if(ui_->actionVertical_Layout->isChecked())
					{
						deltaY = ui_->graphicsView_A->height()/scaleX;
					}
					else
					{
						deltaX = ui_->graphicsView_A->width()/scaleX;
					}

					const KeypointItem * kptA = wordsA.value(ids[i]);
					const KeypointItem * kptB = wordsB.value(ids[i]);
					ui_->graphicsView_A->addLine(
							kptA->rect().x()+kptA->rect().width()/2,
							kptA->rect().y()+kptA->rect().height()/2,
							kptB->rect().x()+kptB->rect().width()/2+deltaX,
							kptB->rect().y()+kptB->rect().height()/2+deltaY,
							ui_->graphicsView_A->getDefaultMatchingLineColor());

					ui_->graphicsView_B->addLine(
							kptA->rect().x()+kptA->rect().width()/2-deltaX,
							kptA->rect().y()+kptA->rect().height()/2-deltaY,
							kptB->rect().x()+kptB->rect().width()/2,
							kptB->rect().y()+kptB->rect().height()/2,
							ui_->graphicsView_B->getDefaultMatchingLineColor());
				}
			}
			ui_->graphicsView_A->update();
			ui_->graphicsView_B->update();
		}
	}
}

void DatabaseViewer::sliderAMoved(int value)
{
	ui_->label_indexA->setText(QString::number(value));
	if(value>=0 && value < ids_.size())
	{
		ui_->label_idA->setText(QString::number(ids_.at(value)));
	}
	else
	{
		ULOGGER_ERROR("Slider index out of range ?");
	}
}

void DatabaseViewer::sliderBMoved(int value)
{
	ui_->label_indexB->setText(QString::number(value));
	if(value>=0 && value < ids_.size())
	{
		ui_->label_idB->setText(QString::number(ids_.at(value)));
	}
	else
	{
		ULOGGER_ERROR("Slider index out of range ?");
	}
}

void DatabaseViewer::update3dView()
{
	if(ui_->dockWidget_view3d->isVisible())
	{
		if(lastSliderIndexBrowsed_ == ui_->horizontalSlider_B->value())
		{
			sliderBValueChanged(ui_->horizontalSlider_B->value());
		}
		else
		{
			sliderAValueChanged(ui_->horizontalSlider_A->value());
		}
	}
}

void DatabaseViewer::sliderNeighborValueChanged(int value)
{
	if(value < neighborLinks_.size())
	{
		this->updateConstraintView(neighborLinks_.at(value));
	}
}

void DatabaseViewer::sliderLoopValueChanged(int value)
{
	if(value < loopLinks_.size())
	{
		this->updateConstraintView(loopLinks_.at(value));
	}
}

// only called when ui_->checkBox_showOptimized state changed
void DatabaseViewer::updateConstraintView()
{
	if(ids_.size())
	{
		Link link = this->findActiveLink(ids_.at(ui_->horizontalSlider_A->value()), ids_.at(ui_->horizontalSlider_B->value()));
		if(link.isValid())
		{
			if(link.type() == Link::kNeighbor ||
			   link.type() == Link::kNeighborMerged)
			{
				this->updateConstraintView(neighborLinks_.at(ui_->horizontalSlider_neighbors->value()), false);
			}
			else
			{
				this->updateConstraintView(loopLinks_.at(ui_->horizontalSlider_loops->value()), false);
			}
		}
	}
}

void DatabaseViewer::updateConstraintView(
		const rtabmap::Link & linkIn,
		bool updateImageSliders,
		const Signature & signatureFrom,
		const Signature & signatureTo)
{
	std::multimap<int, Link>::iterator iterLink = rtabmap::graph::findLink(linksRefined_, linkIn.from(), linkIn.to());
	rtabmap::Link link = linkIn;

	if(iterLink != linksRefined_.end())
	{
		link = iterLink->second;
	}
	else if(ui_->checkBox_ignorePoseCorrection->isChecked())
	{
		if(link.type() == Link::kNeighbor ||
		   link.type() == Link::kNeighborMerged)
		{
			Transform poseFrom = uValue(odomPoses_, link.from(), Transform());
			Transform poseTo = uValue(odomPoses_, link.to(), Transform());
			if(!poseFrom.isNull() && !poseTo.isNull())
			{
				// recompute raw odom transformation and
				// reset to identity covariance
				link = Link(link.from(),
						link.to(),
						link.type(),
						poseFrom.inverse() * poseTo);
			}
		}
	}
	rtabmap::Transform t = link.transform();

	ui_->label_constraint->clear();
	ui_->label_constraint_opt->clear();
	ui_->checkBox_showOptimized->setEnabled(false);
	UASSERT(!t.isNull() && dbDriver_);

	ui_->label_type->setText(QString::number(link.type()));
	ui_->label_type_name->setText(tr("(%1)")
			.arg(link.type()==Link::kNeighbor?"Neighbor":
				 link.type()==Link::kNeighborMerged?"Merged neighbor":
				 link.type()==Link::kGlobalClosure?"Loop closure":
				 link.type()==Link::kLocalSpaceClosure?"Space proximity link":
				 link.type()==Link::kLocalTimeClosure?"Time proximity link":
				 link.type()==Link::kUserClosure?"User link":
				 link.type()==Link::kLandmark?"Landmark link":
				 link.type()==Link::kVirtualClosure?"Virtual link":"Undefined"));
	ui_->label_variance->setText(QString("%1, %2")
			.arg(sqrt(link.transVariance()))
			.arg(sqrt(link.rotVariance())));
	std::stringstream out;
	out << link.infMatrix().inv();
	ui_->lineEdit_covariance->setText(out.str().c_str());
	ui_->label_constraint->setText(QString("%1").arg(t.prettyPrint().c_str()).replace(" ", "\n"));
	if(graphes_.size() &&
	   (int)graphes_.size()-1 == ui_->horizontalSlider_iterations->maximum())
	{
		std::map<int, rtabmap::Transform> & graph = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());

		std::map<int, rtabmap::Transform>::iterator iterFrom = graph.find(link.from());
		std::map<int, rtabmap::Transform>::iterator iterTo = graph.find(link.to());
		if(iterFrom != graph.end() && iterTo != graph.end())
		{
			ui_->checkBox_showOptimized->setEnabled(true);
			Transform topt = iterFrom->second.inverse()*iterTo->second;
			float diff = topt.getDistance(t);
			Transform v1 = t.rotation()*Transform(1,0,0,0,0,0);
			Transform v2 = topt.rotation()*Transform(1,0,0,0,0,0);
			float a = pcl::getAngle3D(Eigen::Vector4f(v1.x(), v1.y(), v1.z(), 0), Eigen::Vector4f(v2.x(), v2.y(), v2.z(), 0));
			a = (a *180.0f) / CV_PI;
			ui_->label_constraint_opt->setText(QString("%1\n(error=%2% a=%3)").arg(QString(topt.prettyPrint().c_str()).replace(" ", "\n")).arg((diff/t.getNorm())*100.0f).arg(a));

			if(ui_->checkBox_showOptimized->isChecked())
			{
				t = topt;
			}
		}
	}

	if(updateImageSliders)
	{
		ui_->horizontalSlider_A->blockSignals(true);
		ui_->horizontalSlider_B->blockSignals(true);
		// set from on left and to on right
		if(link.from()>0)
			ui_->horizontalSlider_A->setValue(idToIndex_.value(link.from()));
		if(link.to() > 0)
			ui_->horizontalSlider_B->setValue(idToIndex_.value(link.to()));
		ui_->horizontalSlider_A->blockSignals(false);
		ui_->horizontalSlider_B->blockSignals(false);
		if(link.from()>0)
			this->update(idToIndex_.value(link.from()),
						ui_->label_indexA,
						ui_->label_parentsA,
						ui_->label_childrenA,
						ui_->label_weightA,
						ui_->label_labelA,
						ui_->label_stampA,
						ui_->graphicsView_A,
						ui_->label_idA,
						ui_->label_mapA,
						ui_->label_poseA,
						ui_->label_velA,
						ui_->label_calibA,
						ui_->label_scanA,
						ui_->label_gravityA,
						ui_->label_gpsA,
						ui_->label_sensorsA,
						false); // don't update constraints view!
		if(link.to()>0)
		{
			this->update(idToIndex_.value(link.to()),
						ui_->label_indexB,
						ui_->label_parentsB,
						ui_->label_childrenB,
						ui_->label_weightB,
						ui_->label_labelB,
						ui_->label_stampB,
						ui_->graphicsView_B,
						ui_->label_idB,
						ui_->label_mapB,
						ui_->label_poseB,
						ui_->label_velB,
						ui_->label_calibB,
						ui_->label_scanB,
						ui_->label_gravityB,
						ui_->label_gpsB,
						ui_->label_sensorsB,
						false); // don't update constraints view!
		}
	}

	if(constraintsViewer_->isVisible())
	{
		SensorData dataFrom, dataTo;

		if(signatureFrom.id()>0)
		{
			dataFrom = signatureFrom.sensorData();
		}
		else
		{
			dbDriver_->getNodeData(link.from(), dataFrom);
		}
		dataFrom.uncompressData();
		UASSERT(dataFrom.imageRaw().empty() || dataFrom.imageRaw().type()==CV_8UC3 || dataFrom.imageRaw().type() == CV_8UC1);
		UASSERT(dataFrom.depthOrRightRaw().empty() || dataFrom.depthOrRightRaw().type()==CV_8UC1 || dataFrom.depthOrRightRaw().type() == CV_16UC1 || dataFrom.depthOrRightRaw().type() == CV_32FC1);

		if(signatureTo.id()>0)
		{
			dataTo = signatureTo.sensorData();
		}
		else
		{
			dbDriver_->getNodeData(link.to(), dataTo);
		}
		dataTo.uncompressData();
		UASSERT(dataTo.imageRaw().empty() || dataTo.imageRaw().type()==CV_8UC3 || dataTo.imageRaw().type() == CV_8UC1);
		UASSERT(dataTo.depthOrRightRaw().empty() || dataTo.depthOrRightRaw().type()==CV_8UC1 || dataTo.depthOrRightRaw().type() == CV_16UC1 || dataTo.depthOrRightRaw().type() == CV_32FC1);

		// get odom pose
		Transform pose = Transform::getIdentity();
		if(ui_->checkBox_odomFrame->isChecked())
		{
			int m,w;
			std::string l;
			double s;
			Transform p,g;
			std::vector<float> v;
			GPS gps;
			EnvSensors sensors;
			dbDriver_->getNodeInfo(link.from(), p, m, w, l, s, g, v, gps, sensors);
			if(!p.isNull())
			{
				// keep just the z and roll/pitch rotation
				float x, y, z, roll, pitch, yaw;
				p.getTranslationAndEulerAngles(x, y, z, roll, pitch, yaw);
				pose = Transform(0,0,z,roll,pitch,0);
			}
		}

		constraintsViewer_->removeCloud("cloud0");
		constraintsViewer_->removeCloud("cloud1");
		//cloud 3d
		if(ui_->checkBox_show3Dclouds->isChecked())
		{
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudFrom, cloudTo;
			pcl::IndicesPtr indicesFrom(new std::vector<int>);
			pcl::IndicesPtr indicesTo(new std::vector<int>);
			if(!dataFrom.imageRaw().empty() && !dataFrom.depthOrRightRaw().empty())
			{
				cloudFrom=util3d::cloudRGBFromSensorData(dataFrom, ui_->spinBox_decimation->value(), 0, 0, indicesFrom.get(), ui_->parameters_toolbox->getParameters());
			}
			if(!dataTo.imageRaw().empty() && !dataTo.depthOrRightRaw().empty())
			{
				cloudTo=util3d::cloudRGBFromSensorData(dataTo, ui_->spinBox_decimation->value(), 0, 0, indicesTo.get(), ui_->parameters_toolbox->getParameters());
			}

			if(cloudTo.get() && indicesTo->size())
			{
				cloudTo = rtabmap::util3d::transformPointCloud(cloudTo, t);
			}

			// Gain compensation
			if(ui_->doubleSpinBox_gainCompensationRadius->value()>0.0 &&
				cloudFrom.get() && indicesFrom->size() &&
				cloudTo.get() && indicesTo->size())
			{
				UTimer t;
				GainCompensator compensator(ui_->doubleSpinBox_gainCompensationRadius->value());
				compensator.feed(cloudFrom, indicesFrom, cloudTo, indicesTo, Transform::getIdentity());
				compensator.apply(0, cloudFrom, indicesFrom);
				compensator.apply(1, cloudTo, indicesTo);
				UINFO("Gain compensation time = %fs", t.ticks());
			}

			if(cloudFrom.get() && indicesFrom->size())
			{
				if(ui_->doubleSpinBox_voxelSize->value() > 0.0)
				{
					cloudFrom = util3d::voxelize(cloudFrom, indicesFrom, ui_->doubleSpinBox_voxelSize->value());
				}
				constraintsViewer_->addCloud("cloud0", cloudFrom, pose, Qt::red);
			}
			if(cloudTo.get() && indicesTo->size())
			{
				if(ui_->doubleSpinBox_voxelSize->value() > 0.0)
				{
					cloudTo = util3d::voxelize(cloudTo, indicesTo, ui_->doubleSpinBox_voxelSize->value());
				}
				constraintsViewer_->addCloud("cloud1", cloudTo, pose, Qt::cyan);
			}
		}

		constraintsViewer_->removeCloud("words0");
		constraintsViewer_->removeCloud("words1");
		if(ui_->checkBox_show3DWords->isChecked())
		{
			std::list<int> ids;
			ids.push_back(link.from());
			ids.push_back(link.to());
			std::list<Signature*> signatures;
			dbDriver_->loadSignatures(ids, signatures);
			if(signatures.size() == 2)
			{
				const Signature * sFrom = signatureFrom.id()>0?&signatureFrom:signatures.front();
				const Signature * sTo = signatureTo.id()>0?&signatureTo:signatures.back();
				UASSERT(sFrom && sTo);
				pcl::PointCloud<pcl::PointXYZ>::Ptr cloudFrom(new pcl::PointCloud<pcl::PointXYZ>);
				pcl::PointCloud<pcl::PointXYZ>::Ptr cloudTo(new pcl::PointCloud<pcl::PointXYZ>);
				cloudFrom->resize(sFrom->getWords3().size());
				cloudTo->resize(sTo->getWords3().size());
				int i=0;
				for(std::multimap<int, cv::Point3f>::const_iterator iter=sFrom->getWords3().begin();
					iter!=sFrom->getWords3().end();
					++iter)
				{
					cloudFrom->at(i++) = pcl::PointXYZ(iter->second.x, iter->second.y, iter->second.z);
				}
				i=0;
				for(std::multimap<int, cv::Point3f>::const_iterator iter=sTo->getWords3().begin();
					iter!=sTo->getWords3().end();
					++iter)
				{
					cloudTo->at(i++) = pcl::PointXYZ(iter->second.x, iter->second.y, iter->second.z);
				}

				if(cloudFrom->size())
				{
					cloudFrom = rtabmap::util3d::removeNaNFromPointCloud(cloudFrom);
				}
				if(cloudTo->size())
				{
					cloudTo = rtabmap::util3d::removeNaNFromPointCloud(cloudTo);
					if(cloudTo->size())
					{
						cloudTo = rtabmap::util3d::transformPointCloud(cloudTo, t);
					}
				}

				if(cloudFrom->size())
				{
					constraintsViewer_->addCloud("words0", cloudFrom, pose, Qt::red);
				}
				else
				{
					UWARN("Empty 3D words for node %d", link.from());
					constraintsViewer_->removeCloud("words0");
				}
				if(cloudTo->size())
				{
					constraintsViewer_->addCloud("words1", cloudTo, pose, Qt::cyan);
				}
				else
				{
					UWARN("Empty 3D words for node %d", link.to());
					constraintsViewer_->removeCloud("words1");
				}
			}
			else
			{
				UERROR("Not found signature %d or %d in RAM", link.from(), link.to());
				constraintsViewer_->removeCloud("words0");
				constraintsViewer_->removeCloud("words1");
			}
			//cleanup
			for(std::list<Signature*>::iterator iter=signatures.begin(); iter!=signatures.end(); ++iter)
			{
				delete *iter;
			}
		}

		constraintsViewer_->removeCloud("scan2");
		constraintsViewer_->removeCloud("scan2normals");
		constraintsViewer_->removeGraph("scan2graph");
		constraintsViewer_->removeCloud("scan0");
		constraintsViewer_->removeCloud("scan1");
		if(ui_->checkBox_show2DScans->isChecked())
		{
			//cloud 2d
			if(link.type() == Link::kLocalSpaceClosure &&
			   !link.userDataCompressed().empty())
			{
				std::vector<int> ids;
				cv::Mat userData = link.uncompressUserDataConst();
				if(userData.type() == CV_8SC1 &&
				   userData.rows == 1 &&
				   userData.cols >= 8 && // including null str ending
				   userData.at<char>(userData.cols-1) == 0 &&
				   memcmp(userData.data, "SCANS:", 6) == 0)
				{
					std::string scansStr = (const char *)userData.data;
					UINFO("Detected \"%s\" in links's user data", scansStr.c_str());
					if(!scansStr.empty())
					{
						std::list<std::string> strs = uSplit(scansStr, ':');
						if(strs.size() == 2)
						{
							std::list<std::string> strIds = uSplit(strs.rbegin()->c_str(), ';');
							for(std::list<std::string>::iterator iter=strIds.begin(); iter!=strIds.end(); ++iter)
							{
								ids.push_back(atoi(iter->c_str()));
								if(ids.back() == link.from())
								{
									ids.pop_back();
								}
							}
						}
					}
				}
				if(ids.size())
				{
					//add other scans matching
					//optimize the path's poses locally

					std::map<int, rtabmap::Transform> poses;
					for(unsigned int i=0; i<ids.size(); ++i)
					{
						if(uContains(odomPoses_, ids[i]))
						{
							poses.insert(*odomPoses_.find(ids[i]));
						}
						else
						{
							UERROR("Not found %d node!", ids[i]);
						}
					}
					if(poses.size())
					{
						Optimizer * optimizer = Optimizer::create(ui_->parameters_toolbox->getParameters());

						UASSERT(uContains(poses, link.to()));
						std::map<int, rtabmap::Transform> posesOut;
						std::multimap<int, rtabmap::Link> linksOut;
						optimizer->getConnectedGraph(
								link.to(),
								poses,
								updateLinksWithModifications(links_),
								posesOut,
								linksOut);

						if(poses.size() != posesOut.size())
						{
							UWARN("Scan poses input and output are different! %d vs %d", (int)poses.size(), (int)posesOut.size());
							UWARN("Input poses: ");
							for(std::map<int, Transform>::iterator iter=poses.begin(); iter!=poses.end(); ++iter)
							{
								UWARN(" %d", iter->first);
							}
							UWARN("Input links: ");
							std::multimap<int, Link> modifiedLinks = updateLinksWithModifications(links_);
							for(std::multimap<int, Link>::iterator iter=modifiedLinks.begin(); iter!=modifiedLinks.end(); ++iter)
							{
								UWARN(" %d->%d", iter->second.from(), iter->second.to());
							}
						}


						QTime time;
						time.start();
						std::map<int, rtabmap::Transform> finalPoses = optimizer->optimize(link.to(), posesOut, linksOut);
						delete optimizer;

						// transform local poses in loop referential
						Transform u = t * finalPoses.at(link.to()).inverse();
						pcl::PointCloud<pcl::PointXYZ>::Ptr assembledScans(new pcl::PointCloud<pcl::PointXYZ>);
						pcl::PointCloud<pcl::PointNormal>::Ptr assembledNormalScans(new pcl::PointCloud<pcl::PointNormal>);
						pcl::PointCloud<pcl::PointXYZ>::Ptr graph(new pcl::PointCloud<pcl::PointXYZ>);
						for(std::map<int, Transform>::iterator iter=finalPoses.begin(); iter!=finalPoses.end(); ++iter)
						{
							iter->second = u * iter->second;
							if(iter->first != link.to()) // already added to view
							{
								//create scan
								SensorData data;
								dbDriver_->getNodeData(iter->first, data);
								LaserScan scan;
								data.uncompressDataConst(0, 0, &scan, 0);
								if(!scan.isEmpty())
								{
									if(scan.hasNormals())
									{
										*assembledNormalScans += *util3d::laserScanToPointCloudNormal(scan, iter->second*scan.localTransform());
									}
									else
									{
										*assembledScans += *util3d::laserScanToPointCloud(scan, iter->second*scan.localTransform());
									}
								}
							}
							graph->push_back(pcl::PointXYZ(iter->second.x(), iter->second.y(), iter->second.z()));
						}

						if(assembledNormalScans->size())
						{
							constraintsViewer_->addCloud("scan2normals", assembledNormalScans, pose, Qt::cyan);
							constraintsViewer_->setCloudColorIndex("scan2normals", 2);
						}
						if(assembledScans->size())
						{
							constraintsViewer_->addCloud("scan2", assembledScans, pose, Qt::cyan);
							constraintsViewer_->setCloudColorIndex("scan2", 2);
						}
						if(graph->size())
						{
							constraintsViewer_->addOrUpdateGraph("scan2graph", graph, Qt::cyan);
						}
					}
				}
			}

			// Added loop closure scans
			constraintsViewer_->removeCloud("scan0");
			constraintsViewer_->removeCloud("scan1");
			if(!dataFrom.laserScanRaw().isEmpty())
			{
				if(dataFrom.laserScanRaw().hasNormals())
				{
					pcl::PointCloud<pcl::PointNormal>::Ptr scan;
					scan = rtabmap::util3d::laserScanToPointCloudNormal(dataFrom.laserScanRaw(), dataFrom.laserScanRaw().localTransform());
					constraintsViewer_->addCloud("scan0", scan, pose, Qt::yellow);
					constraintsViewer_->setCloudColorIndex("scan0", 2);
				}
				else
				{
					pcl::PointCloud<pcl::PointXYZ>::Ptr scan;
					scan = rtabmap::util3d::laserScanToPointCloud(dataFrom.laserScanRaw(), dataFrom.laserScanRaw().localTransform());
					constraintsViewer_->addCloud("scan0", scan, pose, Qt::yellow);
					constraintsViewer_->setCloudColorIndex("scan0", 2);
				}
			}
			if(!dataTo.laserScanRaw().isEmpty())
			{
				if(dataTo.laserScanRaw().hasNormals())
				{
					pcl::PointCloud<pcl::PointNormal>::Ptr scan;
					scan = rtabmap::util3d::laserScanToPointCloudNormal(dataTo.laserScanRaw(), t*dataTo.laserScanRaw().localTransform());
					constraintsViewer_->addCloud("scan1", scan, pose, Qt::magenta);
					constraintsViewer_->setCloudColorIndex("scan1", 2);
				}
				else
				{
					pcl::PointCloud<pcl::PointXYZ>::Ptr scan;
					scan = rtabmap::util3d::laserScanToPointCloud(dataTo.laserScanRaw(), t*dataTo.laserScanRaw().localTransform());
					constraintsViewer_->addCloud("scan1", scan, pose, Qt::magenta);
					constraintsViewer_->setCloudColorIndex("scan1", 2);
				}
			}
		}

		//update coordinate
		constraintsViewer_->addOrUpdateCoordinate("from_coordinate", pose, 0.2);
#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
		constraintsViewer_->addOrUpdateCoordinate("to_coordinate", pose*t, 0.2);
		constraintsViewer_->removeCoordinate("to_coordinate_gt");
		if(uContains(groundTruthPoses_, link.from()) && uContains(groundTruthPoses_, link.to()))
		{
			constraintsViewer_->addOrUpdateCoordinate("to_coordinate_gt",
					pose*(groundTruthPoses_.at(link.from()).inverse()*groundTruthPoses_.at(link.to())), 0.1);
		}
#endif

		constraintsViewer_->clearTrajectory();

		constraintsViewer_->update();
	}

	// update buttons
	updateConstraintButtons();
}

void DatabaseViewer::updateConstraintButtons()
{
	ui_->pushButton_refine->setEnabled(false);
	ui_->pushButton_reset->setEnabled(false);
	ui_->pushButton_add->setEnabled(false);
	ui_->pushButton_reject->setEnabled(false);

	if(ui_->label_type->text().toInt() == Link::kLandmark)
	{
		return;
	}

	int from = ids_.at(ui_->horizontalSlider_A->value());
	int to = ids_.at(ui_->horizontalSlider_B->value());
	if(from!=to && from && to && odomPoses_.find(from) != odomPoses_.end() && odomPoses_.find(to) != odomPoses_.end())
	{
		if((!containsLink(links_, from ,to) && !containsLink(linksAdded_, from ,to)) ||
			containsLink(linksRemoved_, from ,to))
		{
			ui_->pushButton_add->setEnabled(true);
		}
	}

	Link currentLink = findActiveLink(from ,to);

	if(currentLink.isValid() &&
		((currentLink.from() == from && currentLink.to() == to) || (currentLink.from() == to && currentLink.to() == from)))
	{
		if(!containsLink(linksRemoved_, from ,to))
		{
			ui_->pushButton_reject->setEnabled(true);
		}

		//check for modified link
		bool modified = false;
		std::multimap<int, Link>::iterator iter = rtabmap::graph::findLink(linksRefined_, currentLink.from(), currentLink.to());
		if(iter != linksRefined_.end())
		{
			currentLink = iter->second;
			ui_->pushButton_reset->setEnabled(true);
			modified = true;
		}
		if(!modified)
		{
			ui_->pushButton_reset->setEnabled(false);
		}
		ui_->pushButton_refine->setEnabled(true);
	}
}

void DatabaseViewer::sliderIterationsValueChanged(int value)
{
	if(dbDriver_ && value >=0 && value < (int)graphes_.size())
	{
		std::map<int, rtabmap::Transform> graph = uValueAt(graphes_, value);

		std::map<int, Transform> refPoses = groundTruthPoses_;
		if(refPoses.empty())
		{
			refPoses = gpsPoses_;
		}

		// Log ground truth statistics (in TUM's RGBD-SLAM format)
		if(refPoses.size())
		{
			// compute KITTI statistics before aligning the poses
			float length = graph::computePathLength(graph);
			if(refPoses.size() == graph.size() && length >= 100.0f)
			{
				float t_err = 0.0f;
				float r_err = 0.0f;
				graph::calcKittiSequenceErrors(uValues(refPoses), uValues(graph), t_err, r_err);
				UINFO("KITTI t_err = %f %%", t_err);
				UINFO("KITTI r_err = %f deg/m", r_err);
			}

			float translational_rmse = 0.0f;
			float translational_mean = 0.0f;
			float translational_median = 0.0f;
			float translational_std = 0.0f;
			float translational_min = 0.0f;
			float translational_max = 0.0f;
			float rotational_rmse = 0.0f;
			float rotational_mean = 0.0f;
			float rotational_median = 0.0f;
			float rotational_std = 0.0f;
			float rotational_min = 0.0f;
			float rotational_max = 0.0f;

			Transform gtToMap = graph::calcRMSE(
					refPoses,
					graph,
					translational_rmse,
					translational_mean,
					translational_median,
					translational_std,
					translational_min,
					translational_max,
					rotational_rmse,
					rotational_mean,
					rotational_median,
					rotational_std,
					rotational_min,
					rotational_max);

			// ground truth live statistics
			ui_->label_rmse->setNum(translational_rmse);
			UINFO("translational_rmse=%f", translational_rmse);
			UINFO("translational_mean=%f", translational_mean);
			UINFO("translational_median=%f", translational_median);
			UINFO("translational_std=%f", translational_std);
			UINFO("translational_min=%f", translational_min);
			UINFO("translational_max=%f", translational_max);

			UINFO("rotational_rmse=%f", rotational_rmse);
			UINFO("rotational_mean=%f", rotational_mean);
			UINFO("rotational_median=%f", rotational_median);
			UINFO("rotational_std=%f", rotational_std);
			UINFO("rotational_min=%f", rotational_min);
			UINFO("rotational_max=%f", rotational_max);

			if(ui_->checkBox_alignPosesWithGroundTruth->isChecked() && !gtToMap.isIdentity())
			{
				for(std::map<int, Transform>::iterator iter=graph.begin(); iter!=graph.end(); ++iter)
				{
					iter->second = gtToMap * iter->second;
				}
			}
		}

		std::map<int, rtabmap::Transform> graphFiltered;
		if(ui_->checkBox_alignScansCloudsWithGroundTruth->isChecked() && !groundTruthPoses_.empty())
		{
			graphFiltered = groundTruthPoses_;
		}
		else
		{
			graphFiltered = graph;
		}
		if(ui_->groupBox_posefiltering->isChecked())
		{
			graphFiltered = graph::radiusPosesFiltering(graph,
					ui_->doubleSpinBox_posefilteringRadius->value(),
					ui_->doubleSpinBox_posefilteringAngle->value()*CV_PI/180.0);
		}
		std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat> > localMaps;
		std::map<int, std::pair<float, cv::Point3f> > localMapsInfo;
#ifdef RTABMAP_OCTOMAP
		if(octomap_)
		{
			delete octomap_;
			octomap_ = 0;
		}
#endif
		if(ui_->dockWidget_graphView->isVisible() || ui_->dockWidget_occupancyGridView->isVisible())
		{
			//update scans
			UINFO("Update local maps list...");
			std::vector<int> ids = uKeys(graphFiltered);
			for(unsigned int i=0; i<ids.size(); ++i)
			{
				if(generatedLocalMaps_.find(ids[i]) != generatedLocalMaps_.end())
				{
					localMaps.insert(*generatedLocalMaps_.find(ids[i]));
					localMapsInfo.insert(*generatedLocalMapsInfo_.find(ids[i]));
				}
				else if(localMaps_.find(ids[i]) != localMaps_.end())
				{
					if(!localMaps_.find(ids[i])->second.first.first.empty() || !localMaps_.find(ids[i])->second.first.second.empty())
					{
						localMaps.insert(*localMaps_.find(ids.at(i)));
						localMapsInfo.insert(*localMapsInfo_.find(ids[i]));
					}
				}
				else
				{
					SensorData data;
					dbDriver_->getNodeData(ids.at(i), data, false, false, false);
					cv::Mat ground, obstacles, empty;
					data.uncompressData(0, 0, 0, 0, &ground, &obstacles, &empty);
					localMaps_.insert(std::make_pair(ids.at(i), std::make_pair(std::make_pair(ground, obstacles), empty)));
					localMapsInfo_.insert(std::make_pair(ids.at(i), std::make_pair(data.gridCellSize(), data.gridViewPoint())));
					if(!ground.empty() || !obstacles.empty())
					{
						localMaps.insert(std::make_pair(ids.at(i), std::make_pair(std::make_pair(ground, obstacles), empty)));
						localMapsInfo.insert(std::make_pair(ids.at(i), std::make_pair(data.gridCellSize(), data.gridViewPoint())));
					}
				}
			}
			//cleanup
			for(std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat> >::iterator iter=localMaps_.begin(); iter!=localMaps_.end();)
			{
				if(graphFiltered.find(iter->first) == graphFiltered.end())
				{
					localMapsInfo_.erase(iter->first);
					localMaps_.erase(iter++);
				}
				else
				{
					++iter;
				}
			}
			UINFO("Update local maps list... done (%d local maps, graph size=%d)", (int)localMaps.size(), (int)graph.size());
		}

		ParametersMap parameters = ui_->parameters_toolbox->getParameters();
		float cellSize = Parameters::defaultGridCellSize();
		Parameters::parse(parameters, Parameters::kGridCellSize(), cellSize);

		ui_->graphViewer->updateGTGraph(groundTruthPoses_);
		ui_->graphViewer->updateGPSGraph(gpsPoses_, gpsValues_);
		ui_->graphViewer->updateGraph(graph, graphLinks_, mapIds_, weights_);
		if(!ui_->checkBox_wmState->isChecked())
		{
			bool allNodesAreInWM = true;
			std::map<int, float> colors;
			for(std::map<int, rtabmap::Transform>::iterator iter=graph.begin(); iter!=graph.end(); ++iter)
			{
				if(lastWmIds_.find(iter->first) != lastWmIds_.end())
				{
					colors.insert(std::make_pair(iter->first, 1));
				}
				else
				{
					allNodesAreInWM = false;
				}
			}
			if(!allNodesAreInWM)
			{
				ui_->graphViewer->updatePosterior(colors, 1, 1);
			}
		}
		ui_->graphViewer->clearMap();
		occupancyGridViewer_->clear();
		if(graph.size() && localMaps.size() &&
			(ui_->graphViewer->isGridMapVisible() || ui_->dockWidget_occupancyGridView->isVisible()))
		{
			QTime time;
			time.start();

#ifdef RTABMAP_OCTOMAP
			if(ui_->checkBox_octomap->isChecked())
			{
				octomap_ = new OctoMap(parameters);
				bool updateAborted = false;
				for(std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat> >::iterator iter=localMaps.begin(); iter!=localMaps.end(); ++iter)
				{
					if(iter->second.first.first.channels() == 2 || iter->second.first.second.channels() == 2)
					{
						QMessageBox::warning(this, tr(""),
								tr("Some local occupancy grids are 2D, but OctoMap requires 3D local "
									"occupancy grids. Uncheck OctoMap under GUI parameters or generate "
									"3D local occupancy grids (\"Grid/3D\" core parameter)."));
						updateAborted = true;
						break;
					}
					octomap_->addToCache(iter->first, iter->second.first.first, iter->second.first.second, iter->second.second, localMapsInfo.at(iter->first).second);
				}
				if(!updateAborted)
				{
					octomap_->update(graphFiltered);
				}
			}
#endif

			// Generate 2d grid map?
			if((ui_->dockWidget_graphView->isVisible() && ui_->graphViewer->isGridMapVisible()) ||
			   (ui_->dockWidget_occupancyGridView->isVisible() && ui_->checkBox_grid_2d->isChecked()))
			{
				bool eroded = Parameters::defaultGridGlobalEroded();
				Parameters::parse(parameters, Parameters::kGridGlobalEroded(), eroded);
				float xMin, yMin;
				cv::Mat map;

#ifdef RTABMAP_OCTOMAP
				if(ui_->checkBox_octomap->isChecked())
				{
					map = octomap_->createProjectionMap(xMin, yMin, cellSize, 0, ui_->spinBox_grid_depth->value());
				}
				else
#endif
				{
					if(eroded)
					{
						uInsert(parameters, ParametersPair(Parameters::kGridGlobalEroded(), "true"));
					}
					OccupancyGrid grid(parameters);
					grid.setCellSize(cellSize);
					for(std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat> >::iterator iter=localMaps.begin(); iter!=localMaps.end(); ++iter)
					{
						grid.addToCache(iter->first, iter->second.first.first, iter->second.first.second, iter->second.second);
					}
					grid.update(graphFiltered);
					map = grid.getMap(xMin, yMin);
				}

				ui_->label_timeGrid->setNum(double(time.elapsed())/1000.0);

				if(!map.empty())
				{
					cv::Mat map8U = rtabmap::util3d::convertMap2Image8U(map);
					if(ui_->dockWidget_graphView->isVisible() && ui_->graphViewer->isGridMapVisible())
					{
						ui_->graphViewer->updateMap(map8U, cellSize, xMin, yMin);
					}
					if(ui_->dockWidget_occupancyGridView->isVisible() && ui_->checkBox_grid_2d->isChecked())
					{
						occupancyGridViewer_->addOccupancyGridMap(map8U, cellSize, xMin, yMin, 1.0f);
						occupancyGridViewer_->update();
					}
				}
			}

			// Generate 3d grid map?
			if(ui_->dockWidget_occupancyGridView->isVisible())
			{
#ifdef RTABMAP_OCTOMAP
				if(ui_->checkBox_octomap->isChecked())
				{
					updateOctomapView();
				}
				else
#endif
				{
					pcl::PointCloud<pcl::PointXYZ>::Ptr groundXYZ(new pcl::PointCloud<pcl::PointXYZ>);
					pcl::PointCloud<pcl::PointXYZ>::Ptr obstaclesXYZ(new pcl::PointCloud<pcl::PointXYZ>);
					pcl::PointCloud<pcl::PointXYZ>::Ptr emptyCellsXYZ(new pcl::PointCloud<pcl::PointXYZ>);
					pcl::PointCloud<pcl::PointXYZRGB>::Ptr groundRGB(new pcl::PointCloud<pcl::PointXYZRGB>);
					pcl::PointCloud<pcl::PointXYZRGB>::Ptr obstaclesRGB(new pcl::PointCloud<pcl::PointXYZRGB>);
					pcl::PointCloud<pcl::PointXYZRGB>::Ptr emptyCellsRGB(new pcl::PointCloud<pcl::PointXYZRGB>);

					for(std::map<int, std::pair<std::pair<cv::Mat, cv::Mat>, cv::Mat> >::iterator iter=localMaps.begin(); iter!=localMaps.end(); ++iter)
					{
						Transform pose = graphFiltered.at(iter->first);
						float x,y,z,roll,pitch,yaw;
						pose.getTranslationAndEulerAngles(x,y,z,roll,pitch,yaw);
						Transform pose2d(x,y, 0, 0, 0, yaw);
						if(!iter->second.first.first.empty())
						{
							if(iter->second.first.first.channels() == 4)
							{
								*groundRGB += *util3d::laserScanToPointCloudRGB(LaserScan::backwardCompatibility(iter->second.first.first), pose);
							}
							else
							{
								*groundXYZ += *util3d::laserScanToPointCloud(LaserScan::backwardCompatibility(iter->second.first.first), iter->second.first.first.channels()==2?pose2d:pose);
							}
						}
						if(!iter->second.first.second.empty())
						{
							if(iter->second.first.second.channels() == 4)
							{
								*obstaclesRGB += *util3d::laserScanToPointCloudRGB(LaserScan::backwardCompatibility(iter->second.first.second), pose);
							}
							else
							{
								*obstaclesXYZ += *util3d::laserScanToPointCloud(LaserScan::backwardCompatibility(iter->second.first.second), iter->second.first.second.channels()==2?pose2d:pose);
							}
						}
						if(ui_->checkBox_grid_empty->isChecked())
						{
							if(!iter->second.second.empty())
							{
								if(iter->second.second.channels() == 4)
								{
									*emptyCellsRGB += *util3d::laserScanToPointCloudRGB(LaserScan::backwardCompatibility(iter->second.second), pose);
								}
								else
								{
									*emptyCellsXYZ += *util3d::laserScanToPointCloud(LaserScan::backwardCompatibility(iter->second.second), iter->second.second.channels()==2?pose2d:pose);
								}
							}
						}
					}
					// occupancy cloud
					if(groundRGB->size())
					{
						groundRGB = util3d::voxelize(groundRGB, cellSize);
						occupancyGridViewer_->addCloud("groundRGB",
								groundRGB,
								Transform::getIdentity(),
								QColor(ui_->lineEdit_groundColor->text()));
						occupancyGridViewer_->setCloudPointSize("groundRGB", 5);
					}
					if(groundXYZ->size())
					{
						groundXYZ = util3d::voxelize(groundXYZ, cellSize);
						occupancyGridViewer_->addCloud("groundXYZ",
								groundXYZ,
								Transform::getIdentity(),
								QColor(ui_->lineEdit_groundColor->text()));
						occupancyGridViewer_->setCloudPointSize("groundXYZ", 5);
					}
					if(obstaclesRGB->size())
					{
						obstaclesRGB = util3d::voxelize(obstaclesRGB, cellSize);
						occupancyGridViewer_->addCloud("obstaclesRGB",
								obstaclesRGB,
								Transform::getIdentity(),
								QColor(ui_->lineEdit_obstacleColor->text()));
						occupancyGridViewer_->setCloudPointSize("obstaclesRGB", 5);
					}
					if(obstaclesXYZ->size())
					{
						obstaclesXYZ = util3d::voxelize(obstaclesXYZ, cellSize);
						occupancyGridViewer_->addCloud("obstaclesXYZ",
								obstaclesXYZ,
								Transform::getIdentity(),
								QColor(ui_->lineEdit_obstacleColor->text()));
						occupancyGridViewer_->setCloudPointSize("obstaclesXYZ", 5);
					}
					if(emptyCellsRGB->size())
					{
						emptyCellsRGB = util3d::voxelize(emptyCellsRGB, cellSize);
						occupancyGridViewer_->addCloud("emptyCellsRGB",
								emptyCellsRGB,
								Transform::getIdentity(),
								QColor(ui_->lineEdit_emptyColor->text()));
						occupancyGridViewer_->setCloudPointSize("emptyCellsRGB", 5);
						occupancyGridViewer_->setCloudOpacity("emptyCellsRGB", 0.5);
					}
					if(emptyCellsXYZ->size())
					{
						emptyCellsXYZ = util3d::voxelize(emptyCellsXYZ, cellSize);
						occupancyGridViewer_->addCloud("emptyCellsXYZ",
								emptyCellsXYZ,
								Transform::getIdentity(),
								QColor(ui_->lineEdit_emptyColor->text()));
						occupancyGridViewer_->setCloudPointSize("emptyCellsXYZ", 5);
						occupancyGridViewer_->setCloudOpacity("emptyCellsXYZ", 0.5);
					}
					occupancyGridViewer_->update();
				}
			}
		}
		ui_->graphViewer->fitInView(ui_->graphViewer->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
		ui_->graphViewer->update();
		ui_->label_iterations->setNum(value);

		//compute total length (neighbor links)
		float length = 0.0f;
		for(std::multimap<int, rtabmap::Link>::const_iterator iter=graphLinks_.begin(); iter!=graphLinks_.end(); ++iter)
		{
			std::map<int, rtabmap::Transform>::const_iterator jterA = graph.find(iter->first);
			std::map<int, rtabmap::Transform>::const_iterator jterB = graph.find(iter->second.to());
			if(jterA != graph.end() && jterB != graph.end())
			{
				const rtabmap::Transform & poseA = jterA->second;
				const rtabmap::Transform & poseB = jterB->second;
				if(iter->second.type() == rtabmap::Link::kNeighbor ||
				   iter->second.type() == rtabmap::Link::kNeighborMerged)
				{
					Eigen::Vector3f vA, vB;
					float x,y,z;
					poseA.getTranslation(x,y,z);
					vA[0] = x; vA[1] = y; vA[2] = z;
					poseB.getTranslation(x,y,z);
					vB[0] = x; vB[1] = y; vB[2] = z;
					length += (vB - vA).norm();
				}
			}
		}
		ui_->label_pathLength->setNum(length);
	}
}
void DatabaseViewer::updateGraphView()
{
	ui_->label_loopClosures->clear();
	ui_->label_poses->clear();
	ui_->label_rmse->clear();

	if(odomPoses_.size())
	{
		int fromId = ui_->spinBox_optimizationsFrom->value();
		if(!uContains(odomPoses_, fromId))
		{
			QMessageBox::warning(this, tr(""), tr("Graph optimization from id (%1) for which node is not linked to graph.\n Minimum=%2, Maximum=%3")
						.arg(fromId)
						.arg(odomPoses_.begin()->first)
						.arg(odomPoses_.rbegin()->first));
			return;
		}

		std::map<int, Transform> optimizedGraphGuess;
		if(graphes_.size() && useLastOptimizedGraphAsGuess_)
		{
			optimizedGraphGuess = lastOptimizedGraph_;
		}
		else
		{
			optimizedGraphGuess = dbOptimizedPoses_;
		}

		graphes_.clear();
		graphLinks_.clear();

		std::map<int, rtabmap::Transform> poses = odomPoses_;
		if(ui_->checkBox_wmState->isChecked() && uContains(wmStates_, fromId))
		{
			std::map<int, rtabmap::Transform> wmPoses;
			std::vector<int> & wmState = wmStates_.at(fromId);
			for(unsigned int i=0; i<wmState.size(); ++i)
			{
				std::map<int, rtabmap::Transform>::iterator iter = poses.find(wmState[i]);
				if(iter!=poses.end())
				{
					wmPoses.insert(*iter);
				}
			}
			if(!wmPoses.empty())
			{
				poses = wmPoses;
			}
			else
			{
				UWARN("Empty WM poses!? Ignoring WM state... (root id=%d, wmState=%d)", fromId, wmState.size());
			}
		}

		// filter current map if not spanning to all maps
		if(!ui_->checkBox_spanAllMaps->isChecked() && uContains(mapIds_, fromId) && mapIds_.at(fromId) >= 0)
		{
			int currentMapId = mapIds_.at(fromId);
			for(std::map<int, rtabmap::Transform>::iterator iter=poses.begin(); iter!=poses.end();)
			{
				if(iter->first>0 && (!uContains(mapIds_, iter->first) || mapIds_.at(iter->first) != currentMapId))
				{
					poses.erase(iter++);
				}
				else
				{
					++iter;
				}
			}
		}

		ui_->menuExport_poses->setEnabled(true);
		std::multimap<int, rtabmap::Link> links = links_;
		loopLinks_.clear();

		// filter current map if not spanning to all maps
		if(!ui_->checkBox_spanAllMaps->isChecked() && uContains(mapIds_, fromId) && mapIds_.at(fromId) >= 0)
		{
			int currentMapId = mapIds_.at(fromId);
			for(std::multimap<int, rtabmap::Link>::iterator iter=links.begin(); iter!=links.end();)
			{
				if((iter->second.from()>0 && (!uContains(mapIds_, iter->second.from()) || mapIds_.at(iter->second.from()) != currentMapId)) ||
					(iter->second.to()>0 && (!uContains(mapIds_, iter->second.to()) || mapIds_.at(iter->second.to()) != currentMapId)))
				{
					links.erase(iter++);
				}
				else
				{
					++iter;
				}
			}
		}

		links = updateLinksWithModifications(links);
		if(ui_->checkBox_ignorePoseCorrection->isChecked())
		{
			for(std::multimap<int, Link>::iterator iter=links.begin(); iter!=links.end(); ++iter)
			{
				if(iter->second.type() == Link::kNeighbor ||
				   iter->second.type() == Link::kNeighborMerged)
				{
					Transform poseFrom = uValue(poses, iter->second.from(), Transform());
					Transform poseTo = uValue(poses, iter->second.to(), Transform());
					if(!poseFrom.isNull() && !poseTo.isNull())
					{
						// reset to identity covariance
						iter->second = Link(
								iter->second.from(),
								iter->second.to(),
								iter->second.type(),
								poseFrom.inverse() * poseTo);
					}
				}
			}
		}

		// filter links
		int totalNeighbor = 0;
		int totalNeighborMerged = 0;
		int totalGlobal = 0;
		int totalLocalTime = 0;
		int totalLocalSpace = 0;
		int totalUser = 0;
		int totalPriors = 0;
		int totalLandmarks = 0;
		int totalGravity = 0;
		for(std::multimap<int, rtabmap::Link>::iterator iter=links.begin(); iter!=links.end();)
		{
			if(iter->second.type() == Link::kNeighbor)
			{
				++totalNeighbor;
			}
			else if(iter->second.type() == Link::kNeighborMerged)
			{
				++totalNeighborMerged;
			}
			else if(iter->second.type() == Link::kGlobalClosure)
			{
				if(ui_->checkBox_ignoreGlobalLoop->isChecked())
				{
					links.erase(iter++);
					continue;
				}
				loopLinks_.push_back(iter->second);
				++totalGlobal;
			}
			else if(iter->second.type() == Link::kLocalSpaceClosure)
			{
				if(ui_->checkBox_ignoreLocalLoopSpace->isChecked())
				{
					links.erase(iter++);
					continue;
				}
				loopLinks_.push_back(iter->second);
				++totalLocalSpace;
			}
			else if(iter->second.type() == Link::kLocalTimeClosure)
			{
				if(ui_->checkBox_ignoreLocalLoopTime->isChecked())
				{
					links.erase(iter++);
					continue;
				}
				loopLinks_.push_back(iter->second);
				++totalLocalTime;
			}
			else if(iter->second.type() == Link::kUserClosure)
			{
				if(ui_->checkBox_ignoreUserLoop->isChecked())
				{
					links.erase(iter++);
					continue;
				}
				loopLinks_.push_back(iter->second);
				++totalUser;
			}
			else if(iter->second.type() == Link::kLandmark)
			{
				UASSERT(iter->second.from() > 0 && iter->second.to() < 0);
				if(poses.find(iter->second.from()) != poses.end() && poses.find(iter->second.to()) == poses.end())
				{
					poses.insert(std::make_pair(iter->second.to(), poses.at(iter->second.from())*iter->second.transform()));
				}
				loopLinks_.push_back(iter->second);
				++totalLandmarks;
			}
			else if(iter->second.type() == Link::kPosePrior)
			{
				++totalPriors;
			}
			else if(iter->second.type() == Link::kGravity)
			{
				++totalGravity;
			}
			else
			{
				loopLinks_.push_back(iter->second);
			}
			++iter;
		}
		updateLoopClosuresSlider();

		ui_->label_loopClosures->setText(tr("(%1, %2, %3, %4, %5, %6, %7, %8, %9)")
				.arg(totalNeighbor)
				.arg(totalNeighborMerged)
				.arg(totalGlobal)
				.arg(totalLocalSpace)
				.arg(totalLocalTime)
				.arg(totalUser)
				.arg(totalPriors)
				.arg(totalLandmarks)
				.arg(totalGravity));

		// remove intermediate nodes?
		if(ui_->checkBox_ignoreIntermediateNodes->isVisible() &&
		   ui_->checkBox_ignoreIntermediateNodes->isChecked())
		{
			for(std::multimap<int, Link>::iterator iter=links.begin(); iter!=links.end(); ++iter)
			{
				if(iter->second.type() == Link::kNeighbor ||
					iter->second.type() == Link::kNeighborMerged)
				{
					Link link = iter->second;
					while(uContains(weights_, link.to()) && weights_.at(link.to()) < 0)
					{
						std::multimap<int, Link>::iterator uter = links.find(link.to());
						if(uter != links.end())
						{
							UASSERT(links.count(link.to()) == 1);
							poses.erase(link.to());
							link = link.merge(uter->second, uter->second.type());
							links.erase(uter);
						}
						else
						{
							break;
						}
					}

					iter->second = link;
				}
			}
		}

		graphes_.push_back(poses);

		Optimizer * optimizer = Optimizer::create(ui_->parameters_toolbox->getParameters());

		std::map<int, rtabmap::Transform> posesOut;
		std::multimap<int, rtabmap::Link> linksOut;
		UINFO("Get connected graph from %d (%d poses, %d links)", fromId, (int)poses.size(), (int)links.size());
		optimizer->getConnectedGraph(
				fromId,
				poses,
				links,
				posesOut,
				linksOut);
		if(optimizedGraphGuess.size() == posesOut.size())
		{
			bool identical=true;
			for(std::map<int, Transform>::iterator iter=posesOut.begin(); iter!=posesOut.end(); ++iter)
			{
				if(!uContains(optimizedGraphGuess, iter->first))
				{
					identical = false;
					break;
				}
			}
			if(identical)
			{
				posesOut = optimizedGraphGuess;
			}
		}
		UINFO("Connected graph of %d poses and %d links", (int)posesOut.size(), (int)linksOut.size());
		QTime time;
		time.start();
		std::map<int, rtabmap::Transform> finalPoses = optimizer->optimize(fromId, posesOut, linksOut, ui_->checkBox_iterativeOptimization->isChecked()?&graphes_:0);
		ui_->label_timeOptimization->setNum(double(time.elapsed())/1000.0);
		graphLinks_ = linksOut;
		if(posesOut.size() && finalPoses.empty())
		{
			UWARN("Optimization failed... (poses=%d, links=%d).", (int)posesOut.size(), (int)linksOut.size());
			if(!optimizer->isCovarianceIgnored() || optimizer->type() != Optimizer::kTypeTORO)
			{
				QMessageBox::warning(this, tr("Graph optimization error!"), tr("Graph optimization has failed. See the terminal for potential errors. "
						"Give it a try with %1=0 and %2=true.").arg(Parameters::kOptimizerStrategy().c_str()).arg(Parameters::kOptimizerVarianceIgnored().c_str()));
			}
			else
			{
				QMessageBox::warning(this, tr("Graph optimization error!"), tr("Graph optimization has failed. See the terminal for potential errors."));
			}
		}
		ui_->label_poses->setNum((int)finalPoses.size());
		graphes_.push_back(finalPoses);
		delete optimizer;
	}
	if(graphes_.size())
	{
		if(ui_->doubleSpinBox_optimizationScale->value()!=-1.0)
		{
			// scale all poses
			for(std::list<std::map<int, Transform> >::iterator iter=graphes_.begin(); iter!=graphes_.end(); ++iter)
			{
				for(std::map<int, Transform>::iterator jter=iter->begin(); jter!=iter->end(); ++jter)
				{
					jter->second = jter->second.clone();
					jter->second.x() *= ui_->doubleSpinBox_optimizationScale->value();
					jter->second.y() *= ui_->doubleSpinBox_optimizationScale->value();
					jter->second.z() *= ui_->doubleSpinBox_optimizationScale->value();
				}
			}
		}

		ui_->horizontalSlider_iterations->setMaximum((int)graphes_.size()-1);
		ui_->horizontalSlider_iterations->setValue((int)graphes_.size()-1);
		ui_->horizontalSlider_iterations->setEnabled(true);
		ui_->spinBox_optimizationsFrom->setEnabled(true);
		sliderIterationsValueChanged((int)graphes_.size()-1);
	}
	else
	{
		ui_->horizontalSlider_iterations->setEnabled(false);
		ui_->spinBox_optimizationsFrom->setEnabled(false);
	}
}

void DatabaseViewer::updateGrid()
{
	if(sender() == ui_->checkBox_grid_2d && !ui_->checkBox_grid_2d->isChecked())
	{
		//just remove map in occupancy grid view
		occupancyGridViewer_->removeOccupancyGridMap();
		occupancyGridViewer_->update();
	}
	else
	{
		ui_->comboBox_octomap_rendering_type->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->spinBox_grid_depth->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->checkBox_grid_empty->setVisible(!ui_->checkBox_octomap->isChecked() || ui_->comboBox_octomap_rendering_type->currentIndex()==0);
		ui_->label_octomap_cubes->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->label_octomap_depth->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->label_octomap_empty->setVisible(!ui_->checkBox_octomap->isChecked() || ui_->comboBox_octomap_rendering_type->currentIndex()==0);

		update3dView();
		updateGraphView();
	}
}

void DatabaseViewer::updateOctomapView()
{
#ifdef RTABMAP_OCTOMAP
		ui_->comboBox_octomap_rendering_type->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->spinBox_grid_depth->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->checkBox_grid_empty->setVisible(!ui_->checkBox_octomap->isChecked() || ui_->comboBox_octomap_rendering_type->currentIndex()==0);
		ui_->label_octomap_cubes->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->label_octomap_depth->setVisible(ui_->checkBox_octomap->isChecked());
		ui_->label_octomap_empty->setVisible(!ui_->checkBox_octomap->isChecked() || ui_->comboBox_octomap_rendering_type->currentIndex()==0);

		if(ui_->checkBox_octomap->isChecked())
		{
			if(octomap_)
			{
				occupancyGridViewer_->removeOctomap();
				occupancyGridViewer_->removeCloud("octomap_obstacles");
				occupancyGridViewer_->removeCloud("octomap_empty");
				if(ui_->comboBox_octomap_rendering_type->currentIndex()>0)
				{
					occupancyGridViewer_->addOctomap(octomap_, ui_->spinBox_grid_depth->value(), ui_->comboBox_octomap_rendering_type->currentIndex()>1);
				}
				else
				{
					pcl::IndicesPtr obstacles(new std::vector<int>);
					pcl::IndicesPtr empty(new std::vector<int>);
					pcl::IndicesPtr ground(new std::vector<int>);
					pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud = octomap_->createCloud(ui_->spinBox_grid_depth->value(), obstacles.get(), empty.get(), ground.get());

					if(octomap_->hasColor())
					{
						pcl::PointCloud<pcl::PointXYZRGB>::Ptr obstaclesCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
						pcl::copyPointCloud(*cloud, *obstacles, *obstaclesCloud);
						occupancyGridViewer_->addCloud("octomap_obstacles", obstaclesCloud, Transform::getIdentity(), QColor(ui_->lineEdit_obstacleColor->text()));
						occupancyGridViewer_->setCloudPointSize("octomap_obstacles", 5);

						pcl::PointCloud<pcl::PointXYZRGB>::Ptr groundCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
						pcl::copyPointCloud(*cloud, *ground, *groundCloud);
						occupancyGridViewer_->addCloud("octomap_ground", groundCloud, Transform::getIdentity(), QColor(ui_->lineEdit_groundColor->text()));
						occupancyGridViewer_->setCloudPointSize("octomap_ground", 5);
					}
					else
					{
						pcl::PointCloud<pcl::PointXYZ>::Ptr obstaclesCloud(new pcl::PointCloud<pcl::PointXYZ>);
						pcl::copyPointCloud(*cloud, *obstacles, *obstaclesCloud);
						occupancyGridViewer_->addCloud("octomap_obstacles", obstaclesCloud, Transform::getIdentity(), QColor(ui_->lineEdit_obstacleColor->text()));
						occupancyGridViewer_->setCloudPointSize("octomap_obstacles", 5);

						pcl::PointCloud<pcl::PointXYZ>::Ptr groundCloud(new pcl::PointCloud<pcl::PointXYZ>);
						pcl::copyPointCloud(*cloud, *ground, *groundCloud);
						occupancyGridViewer_->addCloud("octomap_ground", groundCloud, Transform::getIdentity(), QColor(ui_->lineEdit_groundColor->text()));
						occupancyGridViewer_->setCloudPointSize("octomap_ground", 5);
					}

					if(ui_->checkBox_grid_empty->isChecked())
					{
						pcl::PointCloud<pcl::PointXYZ>::Ptr emptyCloud(new pcl::PointCloud<pcl::PointXYZ>);
						pcl::copyPointCloud(*cloud, *empty, *emptyCloud);
						occupancyGridViewer_->addCloud("octomap_empty", emptyCloud, Transform::getIdentity(), QColor(ui_->lineEdit_emptyColor->text()));
						occupancyGridViewer_->setCloudOpacity("octomap_empty", 0.5);
						occupancyGridViewer_->setCloudPointSize("octomap_empty", 5);
					}
				}
				occupancyGridViewer_->update();
			}
			if(ui_->dockWidget_view3d->isVisible() && ui_->checkBox_showGrid->isChecked())
			{
				this->update3dView();
			}
		}
#endif
}

Link DatabaseViewer::findActiveLink(int from, int to)
{
	Link link;
	std::multimap<int, Link>::iterator findIter = rtabmap::graph::findLink(linksRefined_, from ,to);
	if(findIter != linksRefined_.end())
	{
		link = findIter->second;
	}
	else
	{
		findIter = rtabmap::graph::findLink(linksAdded_, from ,to);
		if(findIter != linksAdded_.end())
		{
			link = findIter->second;
		}
		else if(!containsLink(linksRemoved_, from ,to))
		{
			findIter = rtabmap::graph::findLink(links_, from ,to);
			if(findIter != links_.end())
			{
				link = findIter->second;
			}
		}
	}
	return link;
}

bool DatabaseViewer::containsLink(std::multimap<int, Link> & links, int from, int to)
{
	return rtabmap::graph::findLink(links, from, to) != links.end();
}

void DatabaseViewer::refineConstraint()
{
	int from = ids_.at(ui_->horizontalSlider_A->value());
	int to = ids_.at(ui_->horizontalSlider_B->value());
	refineConstraint(from, to, false);
}

void DatabaseViewer::refineConstraint(int from, int to, bool silent)
{
	if(from == to)
	{
		UWARN("Cannot refine link to same node");
		return;
	}

	Link currentLink =  findActiveLink(from, to);
	if(!currentLink.isValid())
	{
		UERROR("Not found link! (%d->%d)", from, to);
		return;
	}
	UDEBUG("%d -> %d (type=%d)", from ,to, currentLink.type());
	Transform t = currentLink.transform();
	if(ui_->checkBox_showOptimized->isChecked() &&
	   (currentLink.type() == Link::kNeighbor || currentLink.type() == Link::kNeighborMerged) &&
	   graphes_.size() &&
	   (int)graphes_.size()-1 == ui_->horizontalSlider_iterations->maximum())
	{
		std::map<int, rtabmap::Transform> & graph = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());
		if(currentLink.type() == Link::kNeighbor || currentLink.type() == Link::kNeighborMerged)
		{
			std::map<int, rtabmap::Transform>::iterator iterFrom = graph.find(currentLink.from());
			std::map<int, rtabmap::Transform>::iterator iterTo = graph.find(currentLink.to());
			if(iterFrom != graph.end() && iterTo != graph.end())
			{
				Transform topt = iterFrom->second.inverse()*iterTo->second;
				t = topt;
			}
		}
	}
	else if(ui_->checkBox_ignorePoseCorrection->isChecked() &&
			graph::findLink(linksRefined_, from, to) == linksRefined_.end())
	{
		if(currentLink.type() == Link::kNeighbor ||
		   currentLink.type() == Link::kNeighborMerged)
		{
			Transform poseFrom = uValue(odomPoses_, currentLink.from(), Transform());
			Transform poseTo = uValue(odomPoses_, currentLink.to(), Transform());
			if(!poseFrom.isNull() && !poseTo.isNull())
			{
				t  = poseFrom.inverse() * poseTo; // recompute raw odom transformation
			}
		}
	}

	Transform transform;
	RegistrationInfo info;
	Signature fromS;
	Signature toS;

	SensorData dataFrom;
	dbDriver_->getNodeData(currentLink.from(), dataFrom);

	ParametersMap parameters = ui_->parameters_toolbox->getParameters();

	UTimer timer;

	// Is it a multi-scan proximity detection?
	cv::Mat userData = currentLink.uncompressUserDataConst();
	std::map<int, rtabmap::Transform> scanPoses;

	if(currentLink.type() == Link::kLocalSpaceClosure &&
	   !currentLink.userDataCompressed().empty() &&
	   userData.type() == CV_8SC1 &&
	   userData.rows == 1 &&
	   userData.cols >= 8 && // including null str ending
	   userData.at<char>(userData.cols-1) == 0 &&
	   memcmp(userData.data, "SCANS:", 6) == 0 &&
	   currentLink.from() > currentLink.to())
	{
		std::string scansStr = (const char *)userData.data;
		UINFO("Detected \"%s\" in links's user data", scansStr.c_str());
		if(!scansStr.empty())
		{
			std::list<std::string> strs = uSplit(scansStr, ':');
			if(strs.size() == 2)
			{
				std::list<std::string> strIds = uSplit(strs.rbegin()->c_str(), ';');
				for(std::list<std::string>::iterator iter=strIds.begin(); iter!=strIds.end(); ++iter)
				{
					int id = atoi(iter->c_str());
					if(uContains(odomPoses_, id))
					{
						scanPoses.insert(*odomPoses_.find(id));
					}
					else
					{
						UERROR("Not found %d node!", id);
					}
				}
			}
		}
	}
	if(scanPoses.size())
	{
		//optimize the path's poses locally
		Optimizer * optimizer = Optimizer::create(ui_->parameters_toolbox->getParameters());

		UASSERT(uContains(scanPoses, currentLink.to()));
		std::map<int, rtabmap::Transform> posesOut;
		std::multimap<int, rtabmap::Link> linksOut;
		optimizer->getConnectedGraph(
				currentLink.to(),
				scanPoses,
				updateLinksWithModifications(links_),
				posesOut,
				linksOut);

		if(scanPoses.size() != posesOut.size())
		{
			UWARN("Scan poses input and output are different! %d vs %d", (int)scanPoses.size(), (int)posesOut.size());
			UWARN("Input poses: ");
			for(std::map<int, Transform>::iterator iter=scanPoses.begin(); iter!=scanPoses.end(); ++iter)
			{
				UWARN(" %d", iter->first);
			}
			UWARN("Input links: ");
			std::multimap<int, Link> modifiedLinks = updateLinksWithModifications(links_);
			for(std::multimap<int, Link>::iterator iter=modifiedLinks.begin(); iter!=modifiedLinks.end(); ++iter)
			{
				UWARN(" %d->%d", iter->second.from(), iter->second.to());
			}
		}

		scanPoses = optimizer->optimize(currentLink.to(), posesOut, linksOut);
		delete optimizer;

		std::map<int, Transform> filteredScanPoses = scanPoses;
		float proximityFilteringRadius = 0.0f;
		Parameters::parse(parameters, Parameters::kRGBDProximityPathFilteringRadius(), proximityFilteringRadius);
		if(scanPoses.size() > 2 && proximityFilteringRadius > 0.0f)
		{
			// path filtering
			filteredScanPoses = graph::radiusPosesFiltering(scanPoses, proximityFilteringRadius, 0, true);
			// make sure the current pose is still here
			filteredScanPoses.insert(*scanPoses.find(currentLink.to()));
		}

		Transform toPoseInv = filteredScanPoses.at(currentLink.to()).inverse();
		LaserScan fromScan;
		dataFrom.uncompressData(0,0,&fromScan);
		int maxPoints = fromScan.size();
		pcl::PointCloud<pcl::PointXYZ>::Ptr assembledToClouds(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointNormal>::Ptr assembledToNormalClouds(new pcl::PointCloud<pcl::PointNormal>);
		pcl::PointCloud<pcl::PointXYZI>::Ptr assembledToIClouds(new pcl::PointCloud<pcl::PointXYZI>);
		pcl::PointCloud<pcl::PointXYZINormal>::Ptr assembledToNormalIClouds(new pcl::PointCloud<pcl::PointXYZINormal>);
		for(std::map<int, Transform>::const_iterator iter = filteredScanPoses.begin(); iter!=filteredScanPoses.end(); ++iter)
		{
			if(iter->first != currentLink.from())
			{
				SensorData data;
				dbDriver_->getNodeData(iter->first, data);
				if(!data.laserScanCompressed().isEmpty())
				{
					LaserScan scan;
					data.uncompressData(0, 0, &scan);
					if(!scan.isEmpty() && fromScan.format() == scan.format())
					{
						if(scan.hasIntensity())
						{
							if(scan.hasNormals())
							{
								*assembledToNormalIClouds += *util3d::laserScanToPointCloudINormal(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
							else
							{
								*assembledToIClouds += *util3d::laserScanToPointCloudI(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
						}
						else
						{
							if(scan.hasNormals())
							{
								*assembledToNormalClouds += *util3d::laserScanToPointCloudNormal(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
							else
							{
								*assembledToClouds += *util3d::laserScanToPointCloud(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
						}

						if(scan.size() > maxPoints)
						{
							maxPoints = scan.size();
						}
					}
				}
				else
				{
					UWARN("Laser scan not found for signature %d", iter->first);
				}
			}
		}

		cv::Mat assembledScan;
		if(assembledToNormalClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToNormalClouds):util3d::laserScanFromPointCloud(*assembledToNormalClouds);
		}
		else if(assembledToClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToClouds):util3d::laserScanFromPointCloud(*assembledToClouds);
		}
		else if(assembledToNormalIClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToNormalIClouds):util3d::laserScanFromPointCloud(*assembledToNormalIClouds);
		}
		else if(assembledToIClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToIClouds):util3d::laserScanFromPointCloud(*assembledToIClouds);
		}
		SensorData assembledData;
		// scans are in base frame but for 2d scans, set the height so that correspondences matching works
		assembledData.setLaserScan(LaserScan(
				assembledScan,
				fromScan.maxPoints()?fromScan.maxPoints():maxPoints,
				fromScan.rangeMax(),
				fromScan.format(),
				fromScan.is2d()?Transform(0,0,fromScan.localTransform().z(),0,0,0):Transform::getIdentity()));

		RegistrationIcp registrationIcp(parameters);
		transform = registrationIcp.computeTransformation(dataFrom, assembledData, currentLink.transform(), &info);
		if(!transform.isNull())
		{
			// local scan matching proximity detection should have higher variance (see Rtabmap::process())
			info.covariance*=100.0;
		}
	}
	else
	{
		SensorData dataTo;
		dbDriver_->getNodeData(currentLink.to(), dataTo);
		Registration * registration = Registration::create(parameters);
		if(registration->isScanRequired())
		{
			if(ui_->checkBox_icp_from_depth->isChecked())
			{
				// generate laser scans from depth image
				cv::Mat tmpA, tmpB, tmpC, tmpD;
				dataFrom.uncompressData(&tmpA, &tmpB, 0);
				dataTo.uncompressData(&tmpC, &tmpD, 0);
				pcl::PointCloud<pcl::PointXYZ>::Ptr cloudFrom = util3d::cloudFromSensorData(
						dataFrom,
						ui_->spinBox_icp_decimation->value()==0?1:ui_->spinBox_icp_decimation->value(),
						ui_->doubleSpinBox_icp_maxDepth->value(),
						ui_->doubleSpinBox_icp_minDepth->value(),
						0,
						ui_->parameters_toolbox->getParameters());
				pcl::PointCloud<pcl::PointXYZ>::Ptr cloudTo = util3d::cloudFromSensorData(
						dataTo,
						ui_->spinBox_icp_decimation->value()==0?1:ui_->spinBox_icp_decimation->value(),
						ui_->doubleSpinBox_icp_maxDepth->value(),
						ui_->doubleSpinBox_icp_minDepth->value(),
						0,
						ui_->parameters_toolbox->getParameters());
				int maxLaserScans = cloudFrom->size();
				dataFrom.setLaserScan(LaserScan(util3d::laserScanFromPointCloud(*util3d::removeNaNFromPointCloud(cloudFrom), Transform()), maxLaserScans, 0, LaserScan::kXYZ));
				dataTo.setLaserScan(LaserScan(util3d::laserScanFromPointCloud(*util3d::removeNaNFromPointCloud(cloudTo), Transform()), maxLaserScans, 0, LaserScan::kXYZ));

				if(!dataFrom.laserScanCompressed().isEmpty() || !dataTo.laserScanCompressed().isEmpty())
				{
					UWARN("There are laser scans in data, but generate laser scan from "
						  "depth image option is activated. Ignoring saved laser scans...");
				}
			}
			else
			{
				LaserScan tmpA, tmpB;
				dataFrom.uncompressData(0, 0, &tmpA);
				dataTo.uncompressData(0, 0, &tmpB);
			}
		}

		if(registration->isImageRequired())
		{
			cv::Mat tmpA, tmpB, tmpC, tmpD;
			dataFrom.uncompressData(&tmpA, &tmpB, 0);
			dataTo.uncompressData(&tmpC, &tmpD, 0);
		}

		UINFO("Uncompress time: %f s", timer.ticks());

		fromS = Signature(dataFrom);
		toS = Signature(dataTo);
		transform = registration->computeTransformationMod(fromS, toS, t, &info);
		delete registration;
	}
	UINFO("(%d ->%d) Registration time: %f s", from, to, timer.ticks());

	if(!transform.isNull())
	{
		if(!transform.isIdentity())
		{
			if(info.covariance.at<double>(0,0)<=0.0)
			{
				info.covariance = cv::Mat::eye(6,6,CV_64FC1)*0.0001; // epsilon if exact transform
			}
		}
		Link newLink(currentLink.from(), currentLink.to(), currentLink.type(), transform, info.covariance.inv(), currentLink.userDataCompressed());

		bool updated = false;
		std::multimap<int, Link>::iterator iter = linksRefined_.find(currentLink.from());
		while(iter != linksRefined_.end() && iter->first == currentLink.from())
		{
			if(iter->second.to() == currentLink.to() &&
			   iter->second.type() == currentLink.type())
			{
				iter->second = newLink;
				updated = true;
				break;
			}
			++iter;
		}
		if(!updated)
		{
			linksRefined_.insert(std::make_pair(newLink.from(), newLink));
			updated = true;
		}

		if(updated && !silent)
		{
			this->updateGraphView();
		}

		if(!silent && ui_->dockWidget_constraints->isVisible())
		{
			if(fromS.id() > 0 && toS.id() > 0)
			{
				this->updateConstraintView(newLink, true, fromS, toS);

				ui_->graphicsView_A->setFeatures(fromS.getWords(), fromS.sensorData().depthRaw());
				ui_->graphicsView_B->setFeatures(toS.getWords(), toS.sensorData().depthRaw());
				updateWordsMatching();
			}
			else
			{
				this->updateConstraintView();
			}
		}
	}

	else if(!silent)
	{
		QMessageBox::warning(this,
				tr("Refine link"),
				tr("Cannot find a transformation between nodes %1 and %2: %3").arg(from).arg(to).arg(info.rejectedMsg.c_str()));
	}
}

void DatabaseViewer::addConstraint()
{
	int from = ids_.at(ui_->horizontalSlider_A->value());
	int to = ids_.at(ui_->horizontalSlider_B->value());
	addConstraint(from, to, false);
}

bool DatabaseViewer::addConstraint(int from, int to, bool silent)
{
	bool switchedIds = false;
	if(from == to)
	{
		UWARN("Cannot add link to same node");
		return false;
	}
	else if(from < to)
	{
		switchedIds = true;
		int tmp = from;
		from = to;
		to = tmp;
	}

	Link newLink;
	if(!containsLink(linksAdded_, from, to) &&
	   !containsLink(links_, from, to))
	{
		UASSERT(!containsLink(linksRemoved_, from, to));
		UASSERT(!containsLink(linksRefined_, from, to));

		ParametersMap parameters = ui_->parameters_toolbox->getParameters();
		Registration * reg = Registration::create(parameters);

		bool loopCovLimited = Parameters::defaultRGBDLoopCovLimited();
		Parameters::parse(parameters, Parameters::kRGBDLoopCovLimited(), loopCovLimited);
		std::vector<double> odomMaxInf = odomMaxInf_;
		if(loopCovLimited && odomMaxInf_.empty())
		{
			odomMaxInf = graph::getMaxOdomInf(updateLinksWithModifications(links_));
		}

		Transform t;
		RegistrationInfo info;

		std::list<int> ids;
		ids.push_back(from);
		ids.push_back(to);
		std::list<Signature*> signatures;
		dbDriver_->loadSignatures(ids, signatures);
		if(signatures.size() != 2)
		{
			for(std::list<Signature*>::iterator iter=signatures.begin(); iter!=signatures.end(); ++iter)
			{
				delete *iter;
				return false;
			}
		}
		Signature * fromS = *signatures.begin();
		Signature * toS = *signatures.rbegin();

		bool reextractVisualFeatures = uStr2Bool(parameters.at(Parameters::kRGBDLoopClosureReextractFeatures()));
		if(reg->isScanRequired() ||
			reg->isUserDataRequired() ||
			reextractVisualFeatures)
		{
			// Add sensor data to generate features
			dbDriver_->getNodeData(from, fromS->sensorData(), reextractVisualFeatures, reg->isScanRequired(), reg->isUserDataRequired(), false);
			fromS->sensorData().uncompressData();
			dbDriver_->getNodeData(to, toS->sensorData());
			toS->sensorData().uncompressData();
			if(reextractVisualFeatures)
			{
				fromS->setWords(std::multimap<int, cv::KeyPoint>());
				fromS->setWords3(std::multimap<int, cv::Point3f>());
				fromS->setWordsDescriptors(std::multimap<int, cv::Mat>());
				fromS->sensorData().setFeatures(std::vector<cv::KeyPoint>(), std::vector<cv::Point3f>(), cv::Mat());
				toS->setWords(std::multimap<int, cv::KeyPoint>());
				toS->setWords3(std::multimap<int, cv::Point3f>());
				toS->setWordsDescriptors(std::multimap<int, cv::Mat>());
				toS->sensorData().setFeatures(std::vector<cv::KeyPoint>(), std::vector<cv::Point3f>(), cv::Mat());
			}
		}
		else if(!reextractVisualFeatures && fromS->getWords().empty() && toS->getWords().empty())
		{
			UWARN("\"%s\" is false and signatures (%d and %d) don't have words, "
					"registration will not be possible. Set \"%s\" to true.",
					Parameters::kRGBDLoopClosureReextractFeatures().c_str(),
					fromS->id(),
					toS->id(),
					Parameters::kRGBDLoopClosureReextractFeatures().c_str());
		}

		Transform guess;
		if(!reg->isImageRequired())
		{
			// make a fake guess using globally optimized poses
			if(graphes_.size())
			{
				std::map<int, Transform> optimizedPoses = uValueAt(graphes_, ui_->horizontalSlider_iterations->value());
				if(optimizedPoses.size() > 0)
				{
					std::map<int, Transform>::iterator fromIter = optimizedPoses.find(from);
					std::map<int, Transform>::iterator toIter = optimizedPoses.find(to);
					if(fromIter != optimizedPoses.end() &&
					   toIter != optimizedPoses.end())
					{
						QMessageBox::information(this,
								tr("Add constraint"),
								tr("Registration is done without vision (see %1 parameter), "
									"a guess is taken from the optimized graph.")
									.arg(Parameters::kRegStrategy().c_str()));
						guess = fromIter->second.inverse() * toIter->second;
					}
				}
			}
		}

		t = reg->computeTransformationMod(*fromS, *toS, guess, &info);
		delete reg;
		UDEBUG("");

		if(!silent)
		{
			if(switchedIds)
			{
				ui_->graphicsView_A->setFeatures(toS->getWords(), toS->sensorData().depthRaw());
				ui_->graphicsView_B->setFeatures(fromS->getWords(), fromS->sensorData().depthRaw());
			}
			else
			{
				ui_->graphicsView_A->setFeatures(fromS->getWords(), fromS->sensorData().depthRaw());
				ui_->graphicsView_B->setFeatures(toS->getWords(), toS->sensorData().depthRaw());
			}
			updateWordsMatching();
		}
		
		if(!t.isNull())
		{
			cv::Mat information = info.covariance.inv();
			if(odomMaxInf.size() == 6 && information.cols==6 && information.rows==6)
			{
				for(int i=0; i<6; ++i)
				{
					if(information.at<double>(i,i) > odomMaxInf[i])
					{
						information.at<double>(i,i) = odomMaxInf[i];
					}
				}
			}

			newLink = Link(from, to, Link::kUserClosure, t, information);
		}
		else if(!silent)
		{
			QMessageBox::warning(this,
					tr("Add link"),
					tr("Cannot find a transformation between nodes %1 and %2: %3").arg(from).arg(to).arg(info.rejectedMsg.c_str()));
		}

		for(std::list<Signature*>::iterator iter=signatures.begin(); iter!=signatures.end(); ++iter)
		{
			delete *iter;
		}
	}
	else if(containsLink(linksRemoved_, from, to))
	{
		newLink = rtabmap::graph::findLink(linksRemoved_, from, to)->second;
	}

	bool updateConstraints = newLink.isValid();
	float maxOptimizationError = uStr2Float(ui_->parameters_toolbox->getParameters().at(Parameters::kRGBDOptimizeMaxError()));
	if(newLink.isValid() &&
	   maxOptimizationError > 0.0f &&
	   uStr2Int(ui_->parameters_toolbox->getParameters().at(Parameters::kOptimizerIterations())) > 0.0f)
	{
		int fromId = newLink.from();
		std::multimap<int, Link> linksIn = updateLinksWithModifications(links_);
		linksIn.insert(std::make_pair(newLink.from(), newLink));
		const Link * maxLinearLink = 0;
		const Link * maxAngularLink = 0;
		float maxLinearErrorRatio = 0.0f;
		float maxAngularErrorRatio = 0.0f;
		Optimizer * optimizer = Optimizer::create(ui_->parameters_toolbox->getParameters());
		std::map<int, Transform> poses;
		std::multimap<int, Link> links;
		UASSERT(odomPoses_.find(fromId) != odomPoses_.end());
		UASSERT_MSG(odomPoses_.find(newLink.from()) != odomPoses_.end(), uFormat("id=%d poses=%d links=%d", newLink.from(), (int)odomPoses_.size(), (int)linksIn.size()).c_str());
		UASSERT_MSG(odomPoses_.find(newLink.to()) != odomPoses_.end(), uFormat("id=%d poses=%d links=%d", newLink.to(), (int)odomPoses_.size(), (int)linksIn.size()).c_str());
		optimizer->getConnectedGraph(fromId, odomPoses_, linksIn, poses, links);
		// use already optimized poses
		if(graphes_.size())
		{
			const std::map<int, Transform> & optimizedPoses = graphes_.back();
			for(std::map<int, Transform>::iterator iter = poses.begin(); iter!=poses.end(); ++iter)
			{
				if(optimizedPoses.find(iter->first) != optimizedPoses.end())
				{
					iter->second = optimizedPoses.at(iter->first);
				}
			}
		}
		UASSERT(poses.find(fromId) != poses.end());
		UASSERT_MSG(poses.find(newLink.from()) != poses.end(), uFormat("id=%d poses=%d links=%d", newLink.from(), (int)poses.size(), (int)links.size()).c_str());
		UASSERT_MSG(poses.find(newLink.to()) != poses.end(), uFormat("id=%d poses=%d links=%d", newLink.to(), (int)poses.size(), (int)links.size()).c_str());
		UASSERT(graph::findLink(links, newLink.from(), newLink.to()) != links.end());
		std::map<int, Transform> posesIn = poses;
		poses = optimizer->optimize(fromId, posesIn, links);
		if(posesIn.size() && poses.empty())
		{
			UWARN("Optimization failed... (poses=%d, links=%d).", (int)posesIn.size(), (int)links.size());
		}
		std::string msg;
		if(poses.size())
		{
			float maxLinearError = 0.0f;
			float maxAngularError = 0.0f;
			graph::computeMaxGraphErrors(
					poses,
					links,
					maxLinearErrorRatio,
					maxAngularErrorRatio,
					maxLinearError,
					maxAngularError,
					&maxLinearLink,
					&maxAngularLink);
			if(maxLinearLink)
			{
				UINFO("Max optimization linear error = %f m (link %d->%d, var=%f, ratio error/std=%f)", maxLinearError, maxLinearLink->from(), maxLinearLink->to(), maxLinearLink->transVariance(), maxLinearError/sqrt(maxLinearLink->transVariance()));
				if(maxLinearErrorRatio > maxOptimizationError)
				{
					msg = uFormat("Rejecting edge %d->%d because "
						  "graph error is too large (abs=%f m) after optimization (ratio %f for edge %d->%d, stddev=%f m). "
						  "\"%s\" is %f.",
						  newLink.from(),
						  newLink.to(),
						  maxLinearError,
						  maxLinearErrorRatio,
						  maxLinearLink->from(),
						  maxLinearLink->to(),
						  sqrt(maxLinearLink->transVariance()),
						  Parameters::kRGBDOptimizeMaxError().c_str(),
						  maxOptimizationError);
				}
			}
			if(maxAngularLink)
			{
				UINFO("Max optimization angular error = %f deg (link %d->%d, var=%f, ratio error/std=%f)", maxAngularError*180.0f/CV_PI, maxAngularLink->from(), maxAngularLink->to(), maxAngularLink->rotVariance(), maxAngularError/sqrt(maxAngularLink->rotVariance()));
				if(maxAngularErrorRatio > maxOptimizationError)
				{
					msg = uFormat("Rejecting edge %d->%d because "
						  "graph error is too large (abs=%f deg) after optimization (ratio %f for edge %d->%d, stddev=%f deg). "
						  "\"%s\" is %f.",
						  newLink.from(),
						  newLink.to(),
						  maxAngularError*180.0f/CV_PI,
						  maxAngularErrorRatio,
						  maxAngularLink->from(),
						  maxAngularLink->to(),
						  sqrt(maxAngularLink->rotVariance()),
						  Parameters::kRGBDOptimizeMaxError().c_str(),
						  maxOptimizationError);
				}
			}
		}
		else
		{
			msg = uFormat("Rejecting edge %d->%d because graph optimization has failed!",
					  newLink.from(),
					  newLink.to());
		}
		if(!msg.empty())
		{
			UWARN("%s", msg.c_str());
			if(!silent)
			{
				QMessageBox::warning(this,
						tr("Add link"),
						tr("%1").arg(msg.c_str()));
			}

			updateConstraints = false;
		}
	}

	if(updateConstraints)
	{
		if(containsLink(linksRemoved_, from, to))
		{
			//simply remove from linksRemoved
			linksRemoved_.erase(rtabmap::graph::findLink(linksRemoved_, from, to));
		}
		else
		{
			linksAdded_.insert(std::make_pair(newLink.from(), newLink));
		}
		if(!silent)
		{
			updateLoopClosuresSlider(from, to);
			this->updateGraphView();
		}
	}

	return updateConstraints;
}

void DatabaseViewer::resetConstraint()
{
	int from = ids_.at(ui_->horizontalSlider_A->value());
	int to = ids_.at(ui_->horizontalSlider_B->value());
	if(from < to)
	{
		int tmp = to;
		to = from;
		from = tmp;
	}

	if(from == to)
	{
		UWARN("Cannot reset link to same node");
		return;
	}


	std::multimap<int, Link>::iterator iter = rtabmap::graph::findLink(linksRefined_, from, to);
	if(iter != linksRefined_.end())
	{
		linksRefined_.erase(iter);
		this->updateGraphView();
	}

	iter = rtabmap::graph::findLink(links_, from, to);
	if(iter != links_.end())
	{
		this->updateConstraintView(iter->second);
	}
	iter = rtabmap::graph::findLink(linksAdded_, from, to);
	if(iter != linksAdded_.end())
	{
		this->updateConstraintView(iter->second);
	}
}

void DatabaseViewer::rejectConstraint()
{
	int from = ids_.at(ui_->horizontalSlider_A->value());
	int to = ids_.at(ui_->horizontalSlider_B->value());
	if(from < to)
	{
		int tmp = to;
		to = from;
		from = tmp;
	}

	if(from == to)
	{
		UWARN("Cannot reject link to same node");
		return;
	}

	bool removed = false;

	// find the original one
	std::multimap<int, Link>::iterator iter;
	iter = rtabmap::graph::findLink(links_, from, to);
	if(iter != links_.end())
	{
		if(iter->second.type() == Link::kNeighbor || iter->second.type() == Link::kNeighborMerged)
		{
			QMessageBox::StandardButton button = QMessageBox::warning(this, tr("Reject link"),
					tr("Removing the neighbor link %1->%2 will split the graph. Do you want to continue?").arg(from).arg(to),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
			if(button != QMessageBox::Yes)
			{
				return;
			}
		}
		linksRemoved_.insert(*iter);
		removed = true;
	}

	// remove from refined and added
	iter = rtabmap::graph::findLink(linksRefined_, from, to);
	if(iter != linksRefined_.end())
	{
		linksRefined_.erase(iter);
		removed = true;
	}
	iter = rtabmap::graph::findLink(linksAdded_, from, to);
	if(iter != linksAdded_.end())
	{
		linksAdded_.erase(iter);
		removed = true;
	}
	if(removed)
	{
		this->updateGraphView();
	}
	updateLoopClosuresSlider();
}

std::multimap<int, rtabmap::Link> DatabaseViewer::updateLinksWithModifications(
		const std::multimap<int, rtabmap::Link> & edgeConstraints)
{
	UINFO("linksAdded_=%d linksRefined_=%d linksRemoved_=%d", (int)linksAdded_.size(), (int)linksRefined_.size(), (int)linksRemoved_.size());

	std::multimap<int, rtabmap::Link> links;
	for(std::multimap<int, rtabmap::Link>::const_iterator iter=edgeConstraints.begin();
		iter!=edgeConstraints.end();
		++iter)
	{
		std::multimap<int, rtabmap::Link>::iterator findIter;

		findIter = rtabmap::graph::findLink(linksRemoved_, iter->second.from(), iter->second.to());
		if(findIter != linksRemoved_.end())
		{
			UINFO("Removed link (%d->%d, %d)", iter->second.from(), iter->second.to(), iter->second.type());
			continue; // don't add this link
		}

		findIter = rtabmap::graph::findLink(linksRefined_, iter->second.from(), iter->second.to());
		if(findIter!=linksRefined_.end())
		{
			links.insert(*findIter); // add the refined link
			UINFO("Updated link (%d->%d, %d)", iter->second.from(), iter->second.to(), iter->second.type());
			continue;
		}

		links.insert(*iter); // add original link
	}

	//look for added links
	for(std::multimap<int, rtabmap::Link>::const_iterator iter=linksAdded_.begin();
		iter!=linksAdded_.end();
		++iter)
	{
		std::multimap<int, rtabmap::Link>::iterator findIter = rtabmap::graph::findLink(linksRefined_, iter->second.from(), iter->second.to());
		if(findIter!=linksRefined_.end())
		{
			links.insert(*findIter); // add the refined link
			UINFO("Added refined link (%d->%d, %d)", findIter->second.from(), findIter->second.to(), findIter->second.type());
			continue;
		}

		UINFO("Added link (%d->%d, %d)", iter->second.from(), iter->second.to(), iter->second.type());
		links.insert(*iter);
	}

	return links;
}

void DatabaseViewer::updateLoopClosuresSlider(int from, int to)
{
	int size = loopLinks_.size();
	loopLinks_.clear();
	std::multimap<int, Link> links = updateLinksWithModifications(links_);
	int position = ui_->horizontalSlider_loops->value();
	std::multimap<int, Link> linksSortedByParents;
	for(std::multimap<int, rtabmap::Link>::iterator iter = links.begin(); iter!=links.end(); ++iter)
	{
		if(iter->second.to() > iter->second.from())
		{
			linksSortedByParents.insert(std::make_pair(iter->second.to(), iter->second.inverse()));
		}
		else if(iter->second.to() != iter->second.from())
		{
			linksSortedByParents.insert(*iter);
		}
	}
	for(std::multimap<int, rtabmap::Link>::iterator iter = linksSortedByParents.begin(); iter!=linksSortedByParents.end(); ++iter)
	{
		if(!iter->second.transform().isNull())
		{
			if(iter->second.type() != rtabmap::Link::kNeighbor &&
			   iter->second.type() != rtabmap::Link::kNeighborMerged)
			{
				if((iter->second.from() == from && iter->second.to() == to) ||
				   (iter->second.to() == from && iter->second.from() == to))
				{
					position = loopLinks_.size();
				}
				loopLinks_.append(iter->second);
			}
		}
		else
		{
			UERROR("Transform null for link from %d to %d", iter->first, iter->second.to());
		}
	}

	if(loopLinks_.size())
	{
		if(loopLinks_.size() == 1)
		{
			// just to be able to move the cursor of the loop slider
			loopLinks_.push_back(loopLinks_.front());
		}
		ui_->horizontalSlider_loops->setMinimum(0);
		ui_->horizontalSlider_loops->setMaximum(loopLinks_.size()-1);
		ui_->horizontalSlider_loops->setEnabled(true);
		if(position != ui_->horizontalSlider_loops->value())
		{
			ui_->horizontalSlider_loops->setValue(position);
		}
		else if(size != loopLinks_.size())
		{
			this->updateConstraintView(loopLinks_.at(position));
		}
	}
	else
	{
		ui_->horizontalSlider_loops->setEnabled(false);
		constraintsViewer_->removeAllClouds();
		constraintsViewer_->update();
		updateConstraintButtons();
	}
}

void DatabaseViewer::notifyParametersChanged(const QStringList & parametersChanged)
{
	bool updateStereo = false;
	bool updateGraphView = false;
	for(QStringList::const_iterator iter=parametersChanged.constBegin();
	   iter!=parametersChanged.constEnd() && (!updateStereo || !updateGraphView);
	   ++iter)
	{
		QString group = iter->split('/').first();
		if(!updateStereo && group == "Stereo")
		{
			updateStereo = true;
			continue;
		}
		if(!updateGraphView && group == "Optimize")
		{
			updateGraphView = true;
			continue;
		}
	}

	if(updateStereo)
	{
		this->updateStereo();
	}
	if(updateGraphView)
	{
		this->updateGraphView();
	}
	this->configModified();
}

} // namespace rtabmap
