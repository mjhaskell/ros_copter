#include "mainwindow.hpp"
#include "ui_mainwindow.h"
#include "osgwidget.hpp"
#include "dronenode.hpp"
#include <ros/ros.h>
#include <QToolBar>
#include <QProcess>
#include <QMessageBox>

MainWindow::MainWindow(int argc,char** argv,QWidget *parent) :
    QMainWindow(parent),
    m_ui(new Ui::MainWindow),
    m_argc{argc},
    m_argv{argv},
    m_drone_node{m_argc,m_argv},
    m_process{new QProcess{this}}
{
    m_ui->setupUi(this);

    OSGWidget *osg_widget{new OSGWidget};
    setCentralWidget(osg_widget);
    this->createToolbar();
    this->setupStatusBar();
    m_ui->ros_dock->hide();

    connect(&m_drone_node,&quad::DroneNode::statesChanged,
            osg_widget, &OSGWidget::updateDroneStates);
    connect(&m_drone_node,&quad::DroneNode::rosLostConnection,this,&MainWindow::closeWithWarning);
}

MainWindow::~MainWindow()
{
    delete m_ui;
    if (m_app_started_roscore)
    {
        m_process->close();
        QProcess kill_roscore;
        kill_roscore.start(QString{"killall"}, QStringList() << "-9" << "roscore");
        kill_roscore.waitForFinished();
        kill_roscore.close();

        QProcess kill_rosmaster;
        kill_rosmaster.start(QString{"killall"}, QStringList() << "-9" << "rosmaster");
        kill_rosmaster.waitForFinished();
        kill_rosmaster.close();
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
    while (!m_drone_node.init()) {}
    m_app_started_roscore = true;
}

void MainWindow::createToolbar()
{
    QToolBar *tool_bar{addToolBar(tr("Main Actions Toolbar"))};
    QAction *start_action{createStartAction()};
    tool_bar->addAction(start_action);
    QAction *roscore_action{createRoscoreAction()};
    tool_bar->addAction(roscore_action);

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
    const QIcon start_icon{QIcon(":myicons/start.png")};
    QAction *start_action{new QAction(start_icon, tr("&Run Simulation (Ctrl+R)"), this)};
    start_action->setShortcut(QKeySequence{tr("Ctrl+R")});
    start_action->setStatusTip(tr("Run Simulation"));
    connect(start_action, &QAction::triggered, this, &MainWindow::startSimulation);

    return start_action;
}

void MainWindow::startSimulation()
{
    if (!m_drone_node.startNode())
    {
        if (m_drone_node.rosIsConnected())
            this->closeWithWarning();
        else
            QMessageBox::warning(this,tr("NO ROS MASTER DETECTED!"),
                      tr("Can not start the simulation. Connect to a ros master and try again."));
    }
}

QAction* MainWindow::createRoscoreAction()
{
    const QIcon ros_icon{QIcon(":myicons/ros.png")};
    QAction *start_ros_action{new QAction(ros_icon, tr("&View ROS settings panel"), this)};
    start_ros_action->setStatusTip(tr("Toggle view of ROS settings panel"));
    connect(start_ros_action, &QAction::triggered, this, &MainWindow::on_view_ros_settings_panel_triggered);

    return start_ros_action;
}

void MainWindow::on_start_triggered()
{
    this->startSimulation();
}

void MainWindow::on_close_triggered()
{
    close();
}

void MainWindow::closeWithWarning()
{
    this->updateRosStatus();
    QMessageBox::warning(this,tr("LOST CONNECTION TO ROS MASTER!"),tr("The program can no longer detect the ROS master. It is going to close."));
    close();
}

void MainWindow::on_roscore_button_clicked()
{
    if (!m_drone_node.rosIsConnected())
    {
        m_ui->master_group->setEnabled(false);
        m_ui->core_group->setEnabled(false);
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
    }
    else if(!m_ui->view_ros_connection_status->isChecked())
    {
        m_ui->ros_label->hide();
        m_ui->connection_label->hide();
    }
    m_ui->ros_tab_widget->setEnabled(m_ui->ros_check_box->isChecked());
    m_drone_node.setUseRos(m_ui->ros_check_box->isChecked());
}

void MainWindow::on_view_ros_settings_panel_triggered()
{
    if (m_ui->ros_dock->isVisible())
    {
        m_ui->view_ros_settings_panel->setChecked(false);
        m_ui->ros_dock->hide();
    }
    else
    {
        if (!m_ui->ros_check_box->isChecked())
            m_ui->ros_tab_widget->setEnabled(false);
        m_ui->view_ros_settings_panel->setChecked(true);
        m_ui->ros_dock->show();
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
    m_ui->statusbar->showMessage(tr("Trying to connect to ROS master"));
    if (m_ui->use_env_check_box->isChecked())
    {
        if (!m_drone_node.init())
            QMessageBox::warning(this,tr("NO ROS MASTER DETECTED!"),tr("Make sure roscore is running and try again."));
        else
        {
            m_ui->statusbar->showMessage(tr("Successfully connected to ROS master"));
            m_ui->master_group->setEnabled(false);
            m_ui->core_group->setEnabled(false);
            this->updateRosStatus();
        }
    }
    else
    {
        if (!m_drone_node.init(m_ui->master_line_edit->text().toStdString(),m_ui->host_line_edit->text().toStdString(),m_use_ros_ip))
            QMessageBox::warning(this,tr("NO ROS MASTER DETECTED!"),tr("Make sure roscore is running on specified ROS_MASTER_URI and try again."));
        else
        {
            m_ui->statusbar->showMessage(tr("Successfully connected to ROS master"));
            m_ui->master_group->setEnabled(false);
            m_ui->core_group->setEnabled(false);
            m_ui->master_line_edit->setReadOnly(true);
            m_ui->host_line_edit->setReadOnly(true);
            this->updateRosStatus();
        }
    }
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
