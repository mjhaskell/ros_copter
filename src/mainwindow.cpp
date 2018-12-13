#include "mainwindow.hpp"
#include "ui_mainwindow.h"
#include "osgwidget.hpp"
#include <ros/ros.h>
#include <QToolBar>
#include <QProcess>
#include <QMessageBox>
#include <QSettings>

MainWindow::MainWindow(int argc,char** argv,QWidget *parent) :
    QMainWindow(parent),
    m_ui{new Ui::MainWindow},
    m_osg_widget{new OSGWidget},
    m_argc{argc},
    m_argv{argv},
    m_drone_node{m_argc,m_argv},
    m_process{new QProcess{this}}
{
    m_ui->setupUi(this);
    setCentralWidget(m_osg_widget);
    this->createToolbar();
    this->setupStatusBar();
    m_ui->ros_dock->hide();
    m_ui->topics_group->setEnabled(false);
    this->readSettings();
    this->setupSignalsAndSlots();
}

MainWindow::~MainWindow()
{
    if (m_app_started_roscore)
    {
        m_process->close();
        QProcess kill_roscore;
        kill_roscore.start(QString{"killall"}, QStringList{} << "-9" << "roscore");
        kill_roscore.waitForFinished();
        kill_roscore.close();

        QProcess kill_rosmaster;
        kill_rosmaster.start(QString{"killall"}, QStringList{} << "-9" << "rosmaster");
        kill_rosmaster.waitForFinished();
        kill_rosmaster.close();
    }
    delete m_ui;
    delete m_osg_widget;
    delete m_process;
}

void MainWindow::setupSignalsAndSlots()
{
    connect(m_main_toolbar, &QToolBar::visibilityChanged, this, &MainWindow::on_tool_bar_visibilityChanged);
    connect(&m_drone_node, &quad::DroneNode::feedbackStates, &m_controller_node, &quad::ControllerNode::updateStates);
    connect(&m_controller_node, &quad::ControllerNode::sendInputs, &m_drone_node, &quad::DroneNode::updateInputs);
    connect(&m_drone_node, &quad::DroneNode::statesChanged, m_osg_widget, &OSGWidget::updateDroneStates);
    connect(&m_drone_node, &quad::DroneNode::rosLostConnection, this, &MainWindow::closeWithWarning);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_drone_node.stopRunning();
    m_controller_node.stopRunning();
    this->writeSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::readSettings()
{
    QSettings settings{"Qt-Ros Package", "CopterSim"};
    QString master_url{settings.value("master_url",QString{"http://localhost:11311/"}).toString()};
    QString host_url{settings.value("host_url", QString{"localhost"}).toString()};
    m_ui->master_line_edit->setText(master_url);
    m_ui->host_line_edit->setText(host_url);
    bool remember{settings.value("remember_settings", false).toBool()};
    m_ui->remember_check_box->setChecked(remember);
    bool checked{settings.value("use_environment_variables", false).toBool()};
    m_ui->use_env_check_box->setChecked(checked);
    if (checked)
    {
        m_ui->master_line_edit->setEnabled(false);
        m_ui->host_line_edit->setEnabled(false);
        m_ui->ip_button->setEnabled(false);
    }
    m_use_ros_ip = !settings.value("use_ip", false).toBool();
    on_ip_button_clicked();
}

void MainWindow::writeSettings()
{
    QSettings settings("Qt-Ros Package", "CopterSim");
    if (m_ui->remember_check_box->isChecked())
    {
        settings.setValue("master_url",m_ui->master_line_edit->text());
        settings.setValue("host_url",m_ui->host_line_edit->text());
        settings.setValue("use_environment_variables",QVariant{m_ui->use_env_check_box->isChecked()});
        settings.setValue("remember_settings",QVariant{m_ui->remember_check_box->isChecked()});
        settings.setValue("use_ip",QVariant{m_use_ros_ip});
    }
    else
    {
        settings.setValue("master_url",QString{"http://localhost:11311/"});
        settings.setValue("host_url", QString{"localhost"});
        settings.setValue("use_environment_variables", false);
        settings.setValue("remember_settings", false);
        settings.setValue("use_ip", false);
    }
}

void MainWindow::updateRosStatus()
{
    if(m_drone_node.rosIsConnected() && ros::master::check())
        m_ui->connection_label->setPixmap(m_check_icon.pixmap(16,16));
    else
        m_ui->connection_label->setPixmap(m_x_icon.pixmap(16,16));
}

void MainWindow::startRosCore()
{
    QString program{"roscore"};
    m_process->start(program);
    std::string master_uri{"http://localhost:11311/"};
    std::string host_uri{"localhost"};
    bool use_ros_ip{false};
    while (!m_drone_node.init(master_uri,host_uri,use_ros_ip)) {}
    m_app_started_roscore = true;
}

void MainWindow::createToolbar()
{
    QToolBar *tool_bar{addToolBar(tr("Main Toolbar"))};
    QAction *start_action{createStartAction()};
    tool_bar->addAction(start_action);
    QAction *ros_panel_action{createRosPanelAction()};
    tool_bar->addAction(ros_panel_action);

    m_main_toolbar = tool_bar;
}

void MainWindow::setupStatusBar()
{
    this->updateRosStatus();
    m_ui->statusbar->addPermanentWidget(m_ui->ros_label);
    m_ui->statusbar->addPermanentWidget(m_ui->connection_label);
    m_ui->connection_label->hide();
    m_ui->ros_label->hide();
}

QAction* MainWindow::createStartAction()
{
    const QIcon start_icon{QIcon{":myicons/start.png"}};
    QAction *start_action{new QAction(start_icon, tr("Run Simulation (Ctrl+R)"), this)};
    start_action->setShortcut(QKeySequence{tr("Ctrl+R")});
    start_action->setStatusTip(tr("Run Simulation"));
    connect(start_action, &QAction::triggered, this, &MainWindow::startOrResetSim);

    return start_action;
}

void MainWindow::startOrResetSim()
{
    if (!m_is_running)
    {
        if (startSimulation())
        {
            m_is_running = true;
            m_main_toolbar->actions().first()->setIcon(QIcon{":myicons/reset.png"});
            m_main_toolbar->actions().first()->setToolTip(tr("Reset Simulation (Ctrl+R)"));
            m_main_toolbar->actions().first()->setStatusTip(tr("Reset Simulation"));
            m_ui->ros_check_box->setEnabled(false);
            m_ui->start->setEnabled(false);
            m_ui->reset->setEnabled(true);
            m_ui->topics_combo_box->setEnabled(false);
            m_ui->subscribe_button->setEnabled(false);
            m_ui->scan_button->setEnabled(false);
        }
    }
    else
    {
        m_is_running = false;
        resetSimulation();
        m_main_toolbar->actions().first()->setIcon(QIcon{":myicons/start.png"});
        m_main_toolbar->actions().first()->setToolTip(tr("Run Simulation (Ctrl+R)"));
        m_main_toolbar->actions().first()->setStatusTip(tr("Run Simulation"));
        m_ui->ros_check_box->setEnabled(true);
        m_ui->start->setEnabled(true);
        m_ui->reset->setEnabled(false);
        m_ui->topics_combo_box->setEnabled(true);
        m_ui->subscribe_button->setEnabled(true);
        m_ui->scan_button->setEnabled(true);
    }
}

bool MainWindow::startSimulation()
{
    if (!m_drone_node.startNode())
    {
        if (m_drone_node.rosIsConnected())
            this->closeWithWarning();
        else
            QMessageBox::warning(this,tr("NO ROS MASTER DETECTED!"),
                                 tr("Can not start the simulation. Connect to a ros master and try again."));
        return false;
    }
    else
        m_controller_node.startNode();
    return true;
}

void MainWindow::resetSimulation()
{
    m_drone_node.stopRunning();
    m_controller_node.stopRunning();
}

QAction* MainWindow::createRosPanelAction()
{
    const QIcon ros_icon{QIcon(":myicons/ros.png")};
    QAction *ros_panel_action{new QAction(ros_icon, tr("&View ROS settings panel"), this)};
    ros_panel_action->setStatusTip(tr("Toggle view of ROS settings panel"));
    ros_panel_action->setCheckable(true);
    connect(ros_panel_action, &QAction::triggered, this, &MainWindow::on_view_ros_settings_panel_triggered);

    return ros_panel_action;
}

void MainWindow::on_start_triggered()
{
    this->startOrResetSim();
}

void MainWindow::on_close_triggered()
{
    m_drone_node.stopRunning();
    m_controller_node.stopRunning();
    this->writeSettings();
    close();
}

void MainWindow::closeWithWarning()
{
    this->updateRosStatus();
    QMessageBox::warning(this,tr("LOST CONNECTION TO ROS MASTER!"),tr("The program can no longer detect the ROS master. It is going to close."));
    on_close_triggered();
}

void MainWindow::on_roscore_button_clicked()
{
    if (!m_drone_node.rosIsConnected())
    {
        m_ui->master_group->setEnabled(false);
        m_ui->core_group->setEnabled(false);
        m_ui->roscore_button->setStatusTip(tr(""));
        int msg_disp_time{5000};
        m_ui->statusbar->showMessage(tr("Starting ROS core"),msg_disp_time);
        this->startRosCore();
        m_ui->statusbar->showMessage(tr("ROS core has been started"),msg_disp_time);
    }
    this->updateRosStatus();

}

void MainWindow::on_ros_check_box_clicked()
{
    if (m_ui->ros_check_box->isChecked())
    {
        m_ui->ros_label->show();
        m_ui->connection_label->show();
        m_ui->view_ros_connection_status->setChecked(true);
    }
    else
    {
        m_ui->ros_label->hide();
        m_ui->connection_label->hide();
        m_ui->view_ros_connection_status->setChecked(false);
    }
    m_ui->ros_tab_widget->setEnabled(m_ui->ros_check_box->isChecked());
    m_drone_node.setUseRos(m_ui->ros_check_box->isChecked());
}

void MainWindow::on_view_ros_settings_panel_triggered()
{
    if (m_ui->ros_dock->isVisible())
        on_ros_dock_visibilityChanged(false);
    else
    {
        if (!m_ui->ros_check_box->isChecked())
            m_ui->ros_tab_widget->setEnabled(false);
        on_ros_dock_visibilityChanged(true);
    }
}

void MainWindow::on_view_ros_connection_status_triggered()
{
    if (m_ui->ros_label->isVisible())
    {
        m_ui->view_ros_connection_status->setChecked(false);
        m_ui->connection_label->hide();
        m_ui->ros_label->hide();
    }
    else
    {
        m_ui->view_ros_connection_status->setChecked(true);
        m_ui->connection_label->show();
        m_ui->ros_label->show();
    }
}

void MainWindow::on_master_connect_button_clicked()
{
    this->disableOtherConnectionOptions();
    m_ui->statusbar->showMessage(tr("Trying to connect to ROS master"));
    if (m_ui->use_env_check_box->isChecked())
    {
        if (!m_drone_node.init())
            QMessageBox::warning(this,tr("NO ROS MASTER DETECTED!"),tr("Make sure roscore is running and try again."));
        else
            this->onSuccessfulMasterConnection();
    }
    else
    {
        if (!m_drone_node.init(m_ui->master_line_edit->text().toStdString(),m_ui->host_line_edit->text().toStdString(),m_use_ros_ip))
            QMessageBox::warning(this,tr("NO ROS MASTER DETECTED!"),tr("Make sure the provided ROS_MASTER_URI and ROS_IP/HOSTNAME are correct. ROS will not "
                                                                       "let you change these values once you first try to connect. If they are invalid, you "
                                                                       "will need to restart the application and try again. If the values are correct, make "
                                                                       "sure the ROS core is running and try to connect again."));
        else
            this->onSuccessfulMasterConnection();
    }
}

void MainWindow::disableOtherConnectionOptions()
{
    m_ui->master_line_edit->setEnabled(false);
    m_ui->host_line_edit->setEnabled(false);
    m_ui->ip_button->setEnabled(false);
    m_ui->use_env_check_box->setEnabled(false);
    m_ui->roscore_button->setEnabled(false);
}

void MainWindow::onSuccessfulMasterConnection()
{
    m_ui->master_connect_button->setStatusTip(tr(""));
    int display_time{5000};
    m_ui->statusbar->showMessage(tr("Successfully connected to ROS master"),display_time);
    m_ui->master_group->setEnabled(false);
    m_ui->core_group->setEnabled(false);
    this->updateRosStatus();
    this->populateTopicsComboBox();
    m_ui->topics_group->setEnabled(true);
}


void MainWindow::on_use_env_check_box_clicked(bool checked)
{
    if (checked)
    {
        m_ui->master_line_edit->setEnabled(false);
        m_ui->host_line_edit->setEnabled(false);
        m_ui->ip_button->setEnabled(false);
    }
    else
    {
        m_ui->master_line_edit->setEnabled(true);
        m_ui->host_line_edit->setEnabled(true);
        m_ui->ip_button->setEnabled(true);
    }
}

void MainWindow::on_ip_button_clicked()
{
    if (m_use_ros_ip)
    {
       m_ui->ip_button->setText(tr("Use IP"));
       m_ui->ip_label->setText(tr("ROS Hostname:"));
    }
    else
    {
        m_ui->ip_button->setText(tr("Use Hostname"));
        m_ui->ip_label->setText(tr("ROS IP:"));
    }
    m_use_ros_ip = !m_use_ros_ip;
}

void MainWindow::on_ros_dock_visibilityChanged(bool visible)
{
    if (visible)
    {
        m_ui->view_ros_settings_panel->setChecked(true);
        m_ui->ros_dock->show();
        m_main_toolbar->actions().at(1)->setChecked(true);
    }
    else
    {
        if (!m_ui->ros_check_box->isChecked())
            m_ui->ros_tab_widget->setEnabled(false);
        m_ui->view_ros_settings_panel->setChecked(false);
        m_ui->ros_dock->hide();
        m_main_toolbar->actions().at(1)->setChecked(false);
    }
}

void MainWindow::on_tool_bar_visibilityChanged(bool visible)
{
    if (visible)
    {
        m_main_toolbar->show();
        m_ui->view_main_toolbar->setChecked(true);
    }
    else
    {
        m_main_toolbar->hide();
        m_ui->view_main_toolbar->setChecked(false);
    }
}

void MainWindow::populateTopicsComboBox()
{
    m_ui->topics_combo_box->clear();
    std::string topics{m_drone_node.getOdometryTopics()};
    QString str{QString::fromUtf8(topics.c_str())};
    QStringList list{str.split(",")};
    m_ui->topics_combo_box->addItems(list);
    //    m_ui->topics_combo_box->removeItem(m_ui->topics_combo_box->count()-1);
}

void MainWindow::on_scan_button_clicked()
{
    populateTopicsComboBox();
    int display_time{5000};
    m_ui->statusbar->showMessage(tr("Finished scanning for Odometry topics and re-populated list"),display_time);
}

void MainWindow::on_subscribe_button_clicked()
{
    QString topic{m_ui->topics_combo_box->currentText()};
    m_drone_node.setupRosComms(topic.toStdString());
    QString message{"Subscribed to the selected topic: "};
    int display_time{5000};
    m_ui->statusbar->showMessage(message+topic,display_time);
}

void MainWindow::on_reset_triggered()
{
    startOrResetSim();
}

void MainWindow::on_view_main_toolbar_triggered()
{
    if (m_main_toolbar->isVisible())
        on_tool_bar_visibilityChanged(false);
    else
        on_tool_bar_visibilityChanged(true);
}
