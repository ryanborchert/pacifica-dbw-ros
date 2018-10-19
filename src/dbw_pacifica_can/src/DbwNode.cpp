/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2018, New Eagle
 *  Copyright (c) 2015-2018, Dataspeed Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Dataspeed Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#include "DbwNode.h"
#include <dbw_pacifica_can/dispatch.h>

namespace dbw_pacifica_can
{

DbwNode::DbwNode(ros::NodeHandle &node, ros::NodeHandle &priv_nh)
{
  priv_nh.getParam("dbw_dbc_file", dbcFile_);

  // Initialize enable state machine
  prev_enable_ = true;
  enable_ = false;
  override_brake_ = false;
  override_accelerator_pedal_ = false;
  override_steering_ = false;
  override_gear_ = false;
  fault_brakes_ = false;
  fault_accelerator_pedal_ = false;
  fault_steering_ = false;
  fault_steering_cal_ = false;
  fault_watchdog_ = false;
  fault_watchdog_using_brakes_ = false;
  fault_watchdog_warned_ = false;
  timeout_brakes_ = false;
  timeout_accelerator_pedal_ = false;
  timeout_steering_ = false;
  enabled_brakes_ = false;
  enabled_accelerator_pedal_ = false;
  enabled_steering_ = false;
  gear_warned_ = false;

  // Frame ID
  frame_id_ = "base_footprint";
  priv_nh.getParam("frame_id", frame_id_);

  // Buttons (enable/disable)
  buttons_ = true;
  priv_nh.getParam("buttons", buttons_);


  // Ackermann steering parameters
  acker_wheelbase_ = 2.8498; // 112.2 inches
  acker_track_ = 1.5824; // 62.3 inches
  steering_ratio_ = 14.8;
  priv_nh.getParam("ackermann_wheelbase", acker_wheelbase_);
  priv_nh.getParam("ackermann_track", acker_track_);
  priv_nh.getParam("steering_ratio", steering_ratio_);

  // Initialize joint states
  joint_state_.position.resize(JOINT_COUNT);
  joint_state_.velocity.resize(JOINT_COUNT);
  joint_state_.effort.resize(JOINT_COUNT);
  joint_state_.name.resize(JOINT_COUNT);
  joint_state_.name[JOINT_FL] = "wheel_fl"; // Front Left
  joint_state_.name[JOINT_FR] = "wheel_fr"; // Front Right
  joint_state_.name[JOINT_RL] = "wheel_rl"; // Rear Left
  joint_state_.name[JOINT_RR] = "wheel_rr"; // Rear Right
  joint_state_.name[JOINT_SL] = "steer_fl";
  joint_state_.name[JOINT_SR] = "steer_fr";

  // Set up Publishers
  pub_can_ = node.advertise<can_msgs::Frame>("can_tx", 10);
  pub_brake_ = node.advertise<dbw_pacifica_msgs::BrakeReport>("brake_report", 2);
  pub_accel_pedal_ = node.advertise<dbw_pacifica_msgs::AcceleratorPedalReport>("accelerator_pedal_report", 2);
  pub_steering_ = node.advertise<dbw_pacifica_msgs::SteeringReport>("steering_report", 2);
  pub_gear_ = node.advertise<dbw_pacifica_msgs::GearReport>("gear_report", 2);
  pub_wheel_speeds_ = node.advertise<dbw_pacifica_msgs::WheelSpeedReport>("wheel_speed_report", 2);
  pub_wheel_positions_ = node.advertise<dbw_pacifica_msgs::WheelPositionReport>("wheel_position_report", 2);
  pub_tire_pressure_ = node.advertise<dbw_pacifica_msgs::TirePressureReport>("tire_pressure_report", 2);
  pub_surround_ = node.advertise<dbw_pacifica_msgs::SurroundReport>("surround_report", 2);

  pub_imu_ = node.advertise<sensor_msgs::Imu>("imu/data_raw", 10);
  pub_joint_states_ = node.advertise<sensor_msgs::JointState>("joint_states", 10);
  pub_twist_ = node.advertise<geometry_msgs::TwistStamped>("twist", 10);
  pub_vin_ = node.advertise<std_msgs::String>("vin", 1, true);
  pub_driver_input_ = node.advertise<dbw_pacifica_msgs::DriverInputReport>("driver_input_report", 2);
  pub_misc_ = node.advertise<dbw_pacifica_msgs::MiscReport>("misc_report", 2);
  pub_sys_enable_ = node.advertise<std_msgs::Bool>("dbw_enabled", 1, true);
  publishDbwEnabled();

  // Set up Subscribers
  sub_enable_ = node.subscribe("enable", 10, &DbwNode::recvEnable, this, ros::TransportHints().tcpNoDelay(true));
  sub_disable_ = node.subscribe("disable", 10, &DbwNode::recvDisable, this, ros::TransportHints().tcpNoDelay(true));
  sub_can_ = node.subscribe("can_rx", 100, &DbwNode::recvCAN, this, ros::TransportHints().tcpNoDelay(true));
  sub_brake_ = node.subscribe("brake_cmd", 1, &DbwNode::recvBrakeCmd, this, ros::TransportHints().tcpNoDelay(true));
  sub_accelerator_pedal_ = node.subscribe("accelerator_pedal_cmd", 1, &DbwNode::recvAcceleratorPedalCmd, this, ros::TransportHints().tcpNoDelay(true));
  sub_steering_ = node.subscribe("steering_cmd", 1, &DbwNode::recvSteeringCmd, this, ros::TransportHints().tcpNoDelay(true));
  sub_gear_ = node.subscribe("gear_cmd", 1, &DbwNode::recvGearCmd, this, ros::TransportHints().tcpNoDelay(true));
  sub_misc_ = node.subscribe("misc_cmd", 1, &DbwNode::recvMiscCmd, this, ros::TransportHints().tcpNoDelay(true));


  pdu1_relay_pub_ = node.advertise<pdu_msgs::RelayCommand>("/pduB/relay_cmd", 1000);
  count_ = 0;

  dbwDbc_ = NewEagle::DbcBuilder().NewDbc(dbcFile_);

  // Set up Timer
  timer_ = node.createTimer(ros::Duration(1 / 20.0), &DbwNode::timerCallback, this);
}

DbwNode::~DbwNode()
{
}

void DbwNode::recvEnable(const std_msgs::Empty::ConstPtr& msg)
{
  enableSystem();
}

void DbwNode::recvDisable(const std_msgs::Empty::ConstPtr& msg)
{
  disableSystem();
}

void DbwNode::recvCAN(const can_msgs::Frame::ConstPtr& msg)
{

  if (!msg->is_rtr && !msg->is_error) {
    switch (msg->id) {
      case ID_BRAKE_REPORT:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_BRAKE_REPORT);

        if (msg->dlc >= message->GetDlc()) {
          message->SetFrame(msg);

          bool faultCh1 = message->GetSignal("DBW_BrakeFault_Ch1")->GetResult() ? true : false;
          bool faultCh2 = message->GetSignal("DBW_BrakeFault_Ch2")->GetResult() ? true : false;
          //bool dbwSystemFault = message->GetSignal("DBW_MiscFault_BrkRpt")->GetResult() ? true : false;
          //uint8_t watchdog_status = message->GetSignal("DBW_BrakeWatchdogStatus")->GetResult();
          bool brakeSystemFault = message->GetSignal("DBW_BrakeFault")->GetResult() ? true : false;
          bool dbwSystemFault = brakeSystemFault;

          faultBrakes(faultCh1 && faultCh2);
          //faultWatchdog(dbwSystemFault, watchdog_status, brakeSystemFault);
          faultWatchdog(dbwSystemFault, brakeSystemFault);

          overrideBrake(message->GetSignal("DBW_BrakeDriverActivity")->GetResult());
          dbw_pacifica_msgs::BrakeReport brakeReport;
          brakeReport.header.stamp = msg->header.stamp;
          brakeReport.pedal_position  = message->GetSignal("DBW_BrakePedalDriverInput")->GetResult();
          brakeReport.pedal_output = message->GetSignal("DBW_BrakePedalPosnFdbck")->GetResult();

          brakeReport.enabled = message->GetSignal("DBW_BrakeEnabled")->GetResult() ? true : false;
          brakeReport.driver = message->GetSignal("DBW_BrakeDriverActivity")->GetResult() ? true : false;
          brakeReport.watchdog_status.source = watchdog_status;
          brakeReport.fault_brake_system = brakeSystemFault;
          brakeReport.fault_dbw_system =  dbwSystemFault;
          brakeReport.fault_ch2 = faultCh2;

          brakeReport.rolling_counter =  message->GetSignal("DBW_BrakeRollingCntr")->GetResult();

          brakeReport.brake_torque_actual = message->GetSignal("DBW_BrakePcntTorqueActual")->GetResult();

          brakeReport.intervention_active = message->GetSignal("DBW_BrakeInterventionActv")->GetResult() ? true : false;
          brakeReport.intervention_ready = message->GetSignal("DBW_BrakeInterventionReady")->GetResult() ? true : false;

          brakeReport.parking_brake.status = message->GetSignal("DBW_BrakeParkingBrkStatus")->GetResult();

          brakeReport.control_type.value = message->GetSignal("DBW_BrakeCtrlType")->GetResult();

          pub_brake_.publish(brakeReport);
          if (faultCh1 || faultCh2) {
            ROS_WARN_THROTTLE(5.0, "Brake fault.    FLT1: %s FLT2: %s",
                faultCh1 ? "true, " : "false,",
                faultCh2 ? "true, " : "false,");
          }
        }
      }
      break;

      case ID_ACCEL_PEDAL_REPORT:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_ACCEL_PEDAL_REPORT);
        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          bool faultCh1 = message->GetSignal("DBW_AccelPdlFault_Ch1")->GetResult() ? true : false;
          bool faultCh2 = message->GetSignal("DBW_AccelPdlFault_Ch2")->GetResult() ? true : false;
          bool accelPdlSystemFault = message->GetSignal("DBW_AccelPdlFault")->GetResult() ? true : false;
          bool dbwSystemFault = accelPdlSystemFault;

          uint16_t positionFeedback = message->GetSignal("DBW_AccelPdlPosnFdbck")->GetResult(); 

          faultAcceleratorPedal(faultCh1 && faultCh2);
          faultWatchdog(dbwSystemFault, accelPdlSystemFault);

          overrideAcceleratorPedal(message->GetSignal("DBW_AccelPdlDriverActivity")->GetResult());

          dbw_pacifica_msgs::AcceleratorPedalReport accelPedalReprt;
          accelPedalReprt.header.stamp = msg->header.stamp;
          accelPedalReprt.pedal_input  = message->GetSignal("DBW_AccelPdlDriverInput")->GetResult();
          accelPedalReprt.pedal_output = message->GetSignal("DBW_AccelPdlPosnFdbck")->GetResult();
          accelPedalReprt.enabled = message->GetSignal("DBW_AccelPdlEnabled")->GetResult() ? true : false;
          accelPedalReprt.ignore_driver = message->GetSignal("DBW_AccelPdlIgnoreDriver")->GetResult() ? true : false;
          accelPedalReprt.driver = message->GetSignal("DBW_AccelPdlDriverActivity")->GetResult() ? true : false;
          accelPedalReprt.torque_actual = message->GetSignal("DBW_AccelPcntTorqueActual")->GetResult();

          accelPedalReprt.control_type.value = message->GetSignal("DBW_AccelCtrlType")->GetResult();

          accelPedalReprt.rolling_counter =  message->GetSignal("DBW_AccelPdlRollingCntr")->GetResult();

          accelPedalReprt.watchdog_status.source = watchdog_status;
          accelPedalReprt.fault_accel_pedal_system = accelPdlSystemFault;
          accelPedalReprt.fault_dbw_system = dbwSystemFault;
          accelPedalReprt.fault_ch1 = faultCh1;
          accelPedalReprt.fault_ch2 = faultCh2;

          pub_accel_pedal_.publish(accelPedalReprt);

          if (faultCh1 || faultCh2) {
            ROS_WARN_THROTTLE(5.0, "Accelerator Pedal fault. FLT1: %s FLT2: %s",
                faultCh1 ? "true, " : "false,",
                faultCh2 ? "true, " : "false,");
          }
        }
      }
      break;

      case ID_STEERING_REPORT:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_STEERING_REPORT);
        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          bool steeringSystemFault = message->GetSignal("DBW_SteeringFault")->GetResult() ? true : false;
          uint8_t watchdog_status = message->GetSignal("DBW_SteeringWatchdogStatus")->GetResult();

          faultSteering(steeringSystemFault);

          faultWatchdog(dbwSystemFault);
          overrideSteering(message->GetSignal("DBW_SteeringDriverActivity")->GetResult() ? true : false);

          dbw_pacifica_msgs::SteeringReport steeringReport;
          steeringReport.header.stamp = msg->header.stamp;
          steeringReport.steering_wheel_angle = message->GetSignal("DBW_SteeringWhlAngleAct")->GetResult() * (0.1 * M_PI / 180);
          steeringReport.steering_wheel_angle_cmd = message->GetSignal("DBW_SteeringWhlAngleDes")->GetResult() * (0.1 * M_PI / 180);
          steeringReport.steering_wheel_torque = message->GetSignal("DBW_SteeringWhlTorqueCmd")->GetResult() * 0.0625;

          steeringReport.enabled = message->GetSignal("DBW_SteeringEnabled")->GetResult() ? true : false;
          steeringReport.driver_override = message->GetSignal("DBW_SteeringDriverActivity")->GetResult() ? true : false;

          steeringReport.fault_dbw_system = dbwSystemFault;

          steeringReport.watchdog_status.source = watchdog_status;

          steeringReport.rolling_counter =  message->GetSignal("DBW_SteeringRollingCntr")->GetResult();

          steeringReport.control_type.value =  message->GetSignal("DBW_SteeringCtrlType")->GetResult();

          pub_steering_.publish(out);

          publishJointStates(msg->header.stamp, NULL, &out);

          if (steeringSystemFault) {
            ROS_WARN_THROTTLE(5.0, "Steering fault: %s",
                steeringSystemFault ? "true, " : "false,");
          }
        }
      }
      break;

      case ID_GEAR_REPORT:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_GEAR_REPORT);

        if (msg->dlc >= 1) {

          message->SetFrame(msg);

          bool driverActivity = message->GetSignal("DBW_PrndDriverActivity")->GetResult() ? true : false;

          overrideGear(driverActivity);
          dbw_pacifica_msgs::GearReport out;
          out.header.stamp = msg->header.stamp;

          out.enabled = message->GetSignal("DBW_PrndCtrlEnabled")->GetResult() ? true : false;
          out.state.gear = message->GetSignal("DBW_PrndStateActual")->GetResult();
          out.driver_override = driverActivity;
          out.gear_select_system_fault = message->GetSignal("DBW_PrndFault")->GetResult() ? true : false;

          if (msg->dlc >= message->GetDlc()) {
            out.reject = message->GetSignal("DBW_PrndStateReject")->GetResult() ? true : false;
            if (out.reject.value == dbw_pacifica_msgs::GearReject::NONE) {
              gear_warned_ = false;
            } else if (!gear_warned_) {
              gear_warned_ = true;
              switch (out.reject.value) {
                case dbw_pacifica_msgs::GearReject::SHIFT_IN_PROGRESS:
                  ROS_WARN("Gear shift rejected: Shift in progress");
                  break;
                case dbw_pacifica_msgs::GearReject::OVERRIDE:
                  ROS_WARN("Gear shift rejected: Override on brake, Accelerator Pedal, or steering");
                  break;
                case dbw_pacifica_msgs::GearReject::ROTARY_LOW:
                  ROS_WARN("Gear shift rejected: Rotary shifter can't shift to Low");
                  break;
                case dbw_pacifica_msgs::GearReject::ROTARY_PARK:
                  ROS_WARN("Gear shift rejected: Rotary shifter can't shift out of Park");
                  break;
                case dbw_pacifica_msgs::GearReject::VEHICLE:
                  ROS_WARN("Gear shift rejected: Rejected by vehicle, try pressing the brakes");
                  break;
              }
            }
          }
          pub_gear_.publish(out);
        }
      }
      break;

      case ID_REPORT_WHEEL_SPEED:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_REPORT_WHEEL_SPEED);

        if (msg->dlc >= message->GetDlc()) {
          message->SetFrame(msg);

          dbw_pacifica_msgs::WheelSpeedReport out;
          out.header.stamp = msg->header.stamp;

          if (message->GetSignal("DBW_WhlSpdType")->GetResult() == WHEEL_SPEED_MUX0) {
            out.front_left  = message->GetSignal("DBW_WhlRpm_FL")->GetResult();
            out.front_right = message->GetSignal("DBW_WhlRpm_FR")->GetResult();
            out.rear_left   = message->GetSignal("DBW_WhlRpm_RL")->GetResult();
            out.rear_right  = message->GetSignal("DBW_WhlRpm_RR")->GetResult();
          } else if (message->GetSignal("DBW_WhlSpdType")->GetResult() == WHEEL_SPEED_MUX1) {
            out.front_left  = message->GetSignal("DBW_WhlSpd_FL")->GetResult();
            out.front_right = message->GetSignal("DBW_WhlSpd_FR")->GetResult();
            out.rear_left   = message->GetSignal("DBW_WhlSpd_RL")->GetResult();
            out.rear_right  = message->GetSignal("DBW_WhlSpd_RR")->GetResult();
          }

          pub_wheel_speeds_.publish(out);
          publishJointStates(msg->header.stamp, &out, NULL);
        }
      }
      break;

      case ID_REPORT_WHEEL_POSITION:
      {
         NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_REPORT_WHEEL_POSITION);
        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          dbw_pacifica_msgs::WheelPositionReport out;
          out.header.stamp = msg->header.stamp;
          out.front_left  = message->GetSignal("DBW_WhlPulseCnt_FL")->GetResult();
          out.front_right = message->GetSignal("DBW_WhlPulseCnt_FR")->GetResult();
          out.rear_left   = message->GetSignal("DBW_WhlPulseCnt_RL")->GetResult();
          out.rear_right  = message->GetSignal("DBW_WhlPulseCnt_RR")->GetResult();
          out.wheel_pulses_per_rev  = message->GetSignal("DBW_WhlPulsesPerRev")->GetResult();

          pub_wheel_positions_.publish(out);
        }
      }
      break;

      case ID_REPORT_TIRE_PRESSURE:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_REPORT_TIRE_PRESSURE);

        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          dbw_pacifica_msgs::TirePressureReport out;
          out.header.stamp = msg->header.stamp;
          out.front_left  = message->GetSignal("DBW_TirePressFL")->GetResult();
          out.front_right = message->GetSignal("DBW_TirePressFR")->GetResult();
          out.rear_left   = message->GetSignal("DBW_TirePressRL")->GetResult();
          out.rear_right  = message->GetSignal("DBW_TirePressRR")->GetResult();
          pub_tire_pressure_.publish(out);
        }
      }
      break;

      case ID_REPORT_SURROUND:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_REPORT_SURROUND);

        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          dbw_pacifica_msgs::SurroundReport out;
          out.header.stamp = msg->header.stamp;

          out.front_radar_object_distance = message->GetSignal("DBW_RadarFrontObjDist")->GetResult();
          out.rear_radar_object_distance = message->GetSignal("DBW_SonarRearDist")->GetResult();

          out.front_radar_distance_valid = message->GetSignal("DBW_RadarFrontObjDistVld")->GetResult() ? true : false;
          out.parking_sonar_data_valid = message->GetSignal("DBW_SonarVld")->GetResult() ? true : false;

          out.rear_right.status = message->GetSignal("DBW_SonarArcNumRR")->GetResult();
          out.rear_left.status = message->GetSignal("DBW_SonarArcNumRL")->GetResult();
          out.rear_center.status = message->GetSignal("DBW_SonarArcNumRC")->GetResult();

          out.front_right.status = message->GetSignal("DBW_SonarArcNumFR")->GetResult();
          out.front_left.status = message->GetSignal("DBW_SonarArcNumFL")->GetResult();
          out.front_center.status = message->GetSignal("DBW_SonarArcNumFC")->GetResult();

          pub_surround_.publish(out);
        }
      }
      break;

      case ID_VIN:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_VIN);

        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          if (message->GetSignal("DBW_VinMultiplexor")->GetResult() == VIN_MUX_VIN0) {
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
          } else if (message->GetSignal("DBW_VinMultiplexor")->GetResult() == VIN_MUX_VIN1) {
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
          } else if (message->GetSignal("DBW_VinMultiplexor")->GetResult() == VIN_MUX_VIN2) {
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            vin_.push_back(message->GetSignal("DBW_VinDigit_01")->GetResult());
            std_msgs::String msg; msg.data = vin_;
            pub_vin_.publish(msg);
            //ROS_INFO("Detected VIN: %s", vin_.c_str());
          }
        }
      }
      break;

      case ID_REPORT_IMU:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_REPORT_IMU);

        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          sensor_msgs::Imu out;
          out.header.stamp = msg->header.stamp;
          out.header.frame_id = frame_id_;

          out.angular_velocity.z = (double)message->GetSignal("DBW_ImuYawRate_Raw")->GetResult();

          out.linear_acceleration.x = (double)message->GetSignal("DBW_ImuAccelX")->GetResult();
          out.linear_acceleration.y = (double)message->GetSignal("DBW_ImuAccelY")->GetResult();

          pub_imu_.publish(out);
        }
      }
      break;

      case ID_REPORT_DRIVER_INPUT:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_REPORT_DRIVER_INPUT);

        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          dbw_pacifica_msgs::DriverInputReport out;
          out.header.stamp = msg->header.stamp;

          out.turn_signal.value = message->GetSignal("DBW_DrvInptTurnSignal")->GetResult();
          out.high_beam_headlights.status = message->GetSignal("DBW_DrvInptHiBeam")->GetResult();
          out.wiper.status = message->GetSignal("DBW_DrvInptWiper")->GetResult();

          out.cruise_resume_button = message->GetSignal("DBW_DrvInptCruiseResumeBtn")->GetResult() ? true : false;
          out.cruise_cancel_button = message->GetSignal("DBW_DrvInptCruiseCancelBtn")->GetResult() ? true : false;
          out.cruise_accel_button = message->GetSignal("DBW_DrvInptCruiseAccelBtn")->GetResult() ? true : false;
          out.cruise_decel_button = message->GetSignal("DBW_DrvInptCruiseDecelBtn")->GetResult() ? true : false;
          out.cruise_on_off_button = message->GetSignal("DBW_DrvInptCruiseOnOffBtn")->GetResult() ? true : false;

          out.adaptive_cruise_on_off_button = message->GetSignal("DBW_DrvInptAccOnOffBtn")->GetResult() ? true : false;
          out.adaptive_cruise_increase_distance_button = message->GetSignal("DBW_DrvInptAccIncDistBtn")->GetResult() ? true : false;
          out.adaptive_cruise_decrease_distance_button = message->GetSignal("DBW_DrvInptAccDecDistBtn")->GetResult() ? true : false;

          out.door_or_hood_ajar = message->GetSignal("DBW_OccupAnyDoorOrHoodAjar")->GetResult() ? true : false;

          out.airbag_deployed = message->GetSignal("DBW_OccupAnyAirbagDeployed")->GetResult() ? true : false;
          out.any_seatbelt_unbuckled = message->GetSignal("DBW_OccupAnySeatbeltUnbuckled")->GetResult() ? true : false;

          pub_driver_input_.publish(out);
        }
      }
      break;

      case ID_MISC_REPORT:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_MISC_REPORT);

        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          dbw_pacifica_msgs::MiscReport out;
          out.header.stamp = msg->header.stamp;

          out.fuel_level = (double)message->GetSignal("DBW_MiscFuelLvl")->GetResult();

          out.drive_by_wire_enabled = (double)message->GetSignal("DBW_MiscByWireEnbled")->GetResult();
          out.vehicle_speed = (double)message->GetSignal("DBW_MiscVehicleSpeed")->GetResult();

          out.software_build_number = message->GetSignal("DBW_SoftwareBuildNumber")->GetResult();
          out.general_actuator_fault = message->GetSignal("DBW_MiscFault")->GetResult() ? true : false;
          out.by_wire_ready = message->GetSignal("DBW_MiscByWireReady")->GetResult() ? true : false;
          out.general_driver_activity = message->GetSignal("DBW_MiscDriverActivity")->GetResult() ? true : false;
          out.comms_fault = message->GetSignal("DBW_MiscAKitCommFault")->GetResult() ? true : false;        

          pub_misc_.publish(out);
        }
      }
      break;

      case ID_LOW_VOLTAGE_SYSTEM_REPORT:
      {
        NewEagle::DbcMessage* message = dbwDbc_.GetMessageById(ID_LOW_VOLTAGE_SYSTEM_REPORT);

        if (msg->dlc >= message->GetDlc()) {

          message->SetFrame(msg);

          dbw_pacifica_msgs::LowVoltageSystemReport lvSystemReport;
          lvSystemReport.header.stamp = msg->header.stamp;

          lvSystemReport.vehicle_battery_volts = (double)message->GetSignal("DBW_LvVehBattVlt")->GetResult();
          lvSystemReport.vehicle_battery_current = (double)message->GetSignal("DBW_LvBattCurr")->GetResult();
          lvSystemReport.vehicle_alternator_current = (double)message->GetSignal("DBW_LvAlternatorCurr")->GetResult();

          lvSystemReport.aux_battery_volts = (double)message->GetSignal("DBW_LvDbwBattVlt")->GetResult();
          lvSystemReport.aux_dcdc_current = (double)message->GetSignal("DBW_LvDcdcCurr")->GetResult();

          lvSystemReport.aux_battery_contactor = message->GetSignal("DBW_LvBattContactorCmd")->GetResult() ? true : false;
          lvSystemReport.aux_inverter_contactor = message->GetSignal("DBW_LvInvtrContactorCmd")->GetResult() ? true : false;

          pub_misc_.publish(lvSystemReport);
        }        
      }
      break;

      case ID_BRAKE_CMD:
        //ROS_WARN("DBW system: Another node on the CAN bus is commanding the vehicle!!! Subsystem: Brake. Id: 0x%03X", ID_BRAKE_CMD);
        break;
      case ID_ACCELERATOR_PEDAL_CMD:
        //ROS_WARN("DBW system: Another node on the CAN bus is commanding the vehicle!!! Subsystem: Accelerator Pedal. Id: 0x%03X", ID_ACCELERATOR_PEDAL_CMD);
        break;
      case ID_STEERING_CMD:
        //ROS_WARN("DBW system: Another node on the CAN bus is commanding the vehicle!!! Subsystem: Steering. Id: 0x%03X", ID_STEERING_CMD);
        break;
      case ID_GEAR_CMD:
        //ROS_WARN("DBW system: Another node on the CAN bus is commanding the vehicle!!! Subsystem: Shifting. Id: 0x%03X", ID_GEAR_CMD);
        break;
    }
  }
#if 0
  ROS_INFO("ena: %s, clr: %s, brake: %s, Accelerator Pedal: %s, steering: %s, gear: %s",
           enabled() ? "true " : "false",
           clear() ? "true " : "false",
           override_brake_ ? "true " : "false",
           override_accelerator_pedal_ ? "true " : "false",
           override_steering_ ? "true " : "false",
           override_gear_ ? "true " : "false"
       );
#endif
}

void DbwNode::recvBrakeCmd(const dbw_pacifica_msgs::BrakeCmd::ConstPtr& msg)
{
  NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_BrakeCommand");

  message->GetSignal("AKit_BrakeCtrlEnblCmd")->SetResult(0);
  message->GetSignal("AKit_BrakePedalCmd")->SetResult(0);
  message->GetSignal("AKit_BrakePedalCtrlMode")->SetResult(0);
  message->GetSignal("AKit_BrakePedalClearDriverOvrd")->SetResult(0);

  if (enabled()) {
    message->GetSignal("AKit_BrakePedalCmd")->SetResult(msg->pedal_cmd);

    message->GetSignal("AKit_BrakeCtrlEnblCmd")->SetResult(1);
  }

  NewEagle::DbcSignal* cnt = message->GetSignal("AKit_BrakeWatchdogCntr");
  cnt->SetResult(msg->count);

  if (msg->control_mode.value == 1)
  {
    message->GetSignal("AKit_BrakePedalCtrlMode")->SetResult(1);
  }

  if (clear() || msg->clear) {
    message->GetSignal("AKit_BrakePedalClearDriverOvrd")->SetResult(1);
  }

  can_msgs::Frame frame = message->GetFrame();

  pub_can_.publish(frame);
}

void DbwNode::recvAcceleratorPedalCmd(const dbw_pacifica_msgs::AcceleratorPedalCmd::ConstPtr& msg)
{
  NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_AccelPdlCommand");

  message->GetSignal("AKit_AccelPdlEnblCmd")->SetResult(0);
  message->GetSignal("AKit_AccelPdlCmd")->SetResult(0);

  message->GetSignal("AKit_AccelPdlWatchdogCntr")->SetResult(0);
  message->GetSignal("AKit_AccelPdlCtrlMode")->SetResult(0);
  message->GetSignal("AKit_AccelPdlClearDriverOvrd")->SetResult(0);
  message->GetSignal("Akit_AccelPdlIgnoreDriverOvrd")->SetResult(0);

  if (enabled()) {
    message->GetSignal("AKit_AccelPdlCmd")->SetResult(msg->pedal_cmd);

    message->GetSignal("AKit_AccelPdlEnblCmd")->SetResult(1);
  }

  NewEagle::DbcSignal* cnt = message->GetSignal("AKit_AccelPdlWatchdogCntr");
  cnt->SetResult(msg->accelerator_pedal_cmd_rolling_counter);

  if (msg->control_mode.value == 1)
  message->GetSignal("AKit_AccelPdlWatchdogCntr")->SetResult(1);
  {
  }

  if (msg->control_mode.value == 1)
  {
    message->GetSignal("AKit_AccelPdlCtrlMode")->SetResult(1);
  }

  if (clear() || msg->clear) {
    message->GetSignal("AKit_AccelPdlClearDriverOvrd")->SetResult(1);
  }
  if (msg->ignore) {
    message->GetSignal("Akit_AccelPdlIgnoreDriverOvrd")->SetResult(1);
  }

  can_msgs::Frame frame = message->GetFrame();

  pub_can_.publish(frame);
}

void DbwNode::recvSteeringCmd(const dbw_pacifica_msgs::SteeringCmd::ConstPtr& msg)
{
  NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_SteeringCommand");

  message->GetSignal("AKit_SteerCtrlEnblCmd")->SetResult(0);
  message->GetSignal("AKit_SteeringWhlAngleCmd")->SetResult(0);
  message->GetSignal("AKit_SteeringWhlTrqCmd")->SetResult(0);
  message->GetSignal("AKit_SteeringWhlAngleVelocity")->SetResult(0);
  message->GetSignal("AKit_SteeringWhlClearDriverOvrd")->SetResult(0);
  message->GetSignal("AKit_SteeringWhlIgnoreDriverOvrd")->SetResult(0);
  message->GetSignal("AKit_SteeringWhlQuiet")->SetResult(0);
  message->GetSignal("AKit_SteeringWhlCtrlMode")->SetResult(0);

  if (enabled()) {
    message->GetSignal("AKit_SteeringWhlCmdType")->SetResult(msg->command_type.value);

    if (0 == msg->command_type.value)
    {
      double scmd = std::max((float)-5000, std::min((float)5000, (float)(msg->steering_wheel_angle_cmd * (180 / M_PI * 10))));

      scmd /= 10;
      message->GetSignal("AKit_SteeringWhlAngleCmd")->SetResult(scmd);
    }
    else // torque mode
    {
      message->GetSignal("AKit_SteeringWhlTrqCmd")->SetResult(msg->steering_wheel_torque_cmd);
    }

    if (fabsf(msg->steering_wheel_angle_velocity) > 0)
    {
      uint16_t vcmd =  std::max((float)1, std::min((float)254, (float)roundf(fabsf(msg->steering_wheel_angle_velocity) * 180 / M_PI / 2)));

      message->GetSignal("AKit_SteeringWhlAngleVelocity")->SetResult(vcmd);
    }

    message->GetSignal("AKit_SteerCtrlEnblCmd")->SetResult(1);
  }

  if (clear() || msg->clear) {
    message->GetSignal("AKit_SteeringWhlClearDriverOvrd")->SetResult(1);
  }
  if (msg->ignore) {
    message->GetSignal("AKit_SteeringWhlIgnoreDriverOvrd")->SetResult(1);
  }
  if (msg->quiet) {
    message->GetSignal("AKit_SteeringWhlQuiet")->SetResult(1);
  }

  if (msg->control_mode.value == 1)
  {
    message->GetSignal("AKit_SteeringWhlCtrlMode")->SetResult(1);
  }

  message->GetSignal("AKit_SteerCmdWatchdogCntr")->SetResult(msg->count);

  can_msgs::Frame frame = message->GetFrame();

  pub_can_.publish(frame);
}

void DbwNode::recvGearCmd(const dbw_pacifica_msgs::GearCmd::ConstPtr& msg)
{
  NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_GearCommand");

  message->GetSignal("AKit_PrndStateCmd")->SetResult(0);
  message->GetSignal("AKit_PrndClearDriverOvrd")->SetResult(0);

  if (enabled()) {
    message->GetSignal("AKit_PrndStateCmd")->SetResult(msg->cmd.gear);
  }
  if (clear() || msg->clear) {
    message->GetSignal("AKit_PrndClearDriverOvrd")->SetResult(1);
  }

  message->GetSignal("AKit_PrndCmdWatchdogCntr")->SetResult(msg->gear_cmd_rolling_counter);

  can_msgs::Frame frame = message->GetFrame();

  pub_can_.publish(frame);
}

void DbwNode::recvMiscCmd(const dbw_pacifica_msgs::MiscCmd::ConstPtr& msg)
{
  NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_Misc");

  message->GetSignal("AKit_TurnSignalCmd")->SetResult(0);

  if (enabled()) {
    message->GetSignal("AKit_TurnSignalCmd")->SetResult(msg->cmd.value);
  }

  message->GetSignal("AKit_DoorToggleReq")->SetResult(msg->door_request.value);

  message->GetSignal("AKit_MiscCmdWatchdogCntr")->SetResult(msg->misc_cmd_rolling_counter);

  can_msgs::Frame frame = message->GetFrame();

  pub_can_.publish(frame);
}

bool DbwNode::publishDbwEnabled()
{
  bool change = false;
  bool en = enabled();
  if (prev_enable_ != en) {
    std_msgs::Bool msg;
    msg.data = en;
    pub_sys_enable_.publish(msg);
    change = true;
  }
  prev_enable_ = en;
  return change;
}

void DbwNode::timerCallback(const ros::TimerEvent& event)
{
  if (clear()) {
    can_msgs::Frame out;
    out.is_extended = false;

    if (override_brake_) {
      // Might have an issue with WatchdogCntr when these are set.
      NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_BrakeCommand");
      message->GetSignal("AKit_BrakePedalCmd")->SetResult(0);
      message->GetSignal("AKit_BrakeCtrlEnblCmd")->SetResult(0);
      message->GetSignal("AKit_BrakePedalCtrlMode")->SetResult(0);
      message->GetSignal("AKit_BrakePedalClearDriverOvrd")->SetResult(1);
      pub_can_.publish(message->GetFrame());
    }

    if (override_accelerator_pedal_)
    {
      // Might have an issue with WatchdogCntr when these are set.
      NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_AccelPdlCommand");
      message->GetSignal("AKit_AccelPdlCmd")->SetResult(0);
      message->GetSignal("AKit_AccelPdlEnblCmd")->SetResult(0);
      message->GetSignal("Akit_AccelPdlIgnoreDriverOvrd")->SetResult(0);
      message->GetSignal("AKit_AccelPdlCtrlMode")->SetResult(0);
      message->GetSignal("AKit_AccelPdlClearDriverOvrd")->SetResult(1);
      pub_can_.publish(message->GetFrame());
    }

    if (override_steering_) {
      // Might have an issue with WatchdogCntr when these are set.
      NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_SteeringCommand");
      message->GetSignal("AKit_SteeringWhlAngleCmd")->SetResult(0);
      message->GetSignal("AKit_SteeringWhlAngleVelocity")->SetResult(0);
      message->GetSignal("AKit_SteeringWhlIgnoreDriverOvrd")->SetResult(0);
      message->GetSignal("AKit_SteeringWhlQuiet")->SetResult(0);
      message->GetSignal("AKit_SteeringWhlTrqCmd")->SetResult(0);
      message->GetSignal("AKit_SteeringWhlCtrlMode")->SetResult(0);
      message->GetSignal("AKit_SteeringWhlCmdType")->SetResult(0);
      message->GetSignal("AKit_SteeringWhlClearDriverOvrd")->SetResult(1);

      pub_can_.publish(message->GetFrame());
    }

    if (override_gear_) {
      NewEagle::DbcMessage* message = dbwDbc_.GetMessage("AKit_GearCommand");
      message->GetSignal("AKit_PrndStateCmd")->SetResult(0);
      message->GetSignal("AKit_PrndClearDriverOvrd")->SetResult(1);
      pub_can_.publish(message->GetFrame());
    }
  }
}

void DbwNode::enableSystem()
{
  if (!enable_) {
    if (fault()) {
      if (fault_steering_cal_) {
        ROS_WARN("DBW system not enabled. Steering calibration fault.");
      }
      if (fault_brakes_) {
        ROS_WARN("DBW system not enabled. Braking fault.");
      }
      if (fault_accelerator_pedal_) {
        ROS_WARN("DBW system not enabled. Accelerator Pedal fault.");
      }
      if (fault_steering_) {
        ROS_WARN("DBW system not enabled. Steering fault.");
      }
      if (fault_watchdog_) {
        ROS_WARN("DBW system not enabled. Watchdog fault.");
      }
    } else {
      enable_ = true;
      if (publishDbwEnabled()) {
        ROS_INFO("DBW system enabled.");
      } else {
        ROS_INFO("DBW system enable requested. Waiting for ready.");
      }
    }
  }
}

void DbwNode::disableSystem()
{
  if (enable_) {
    enable_ = false;
    publishDbwEnabled();
    ROS_WARN("DBW system disabled.");
  }
}

void DbwNode::buttonCancel()
{
  if (enable_) {
    enable_ = false;
    publishDbwEnabled();
    ROS_WARN("DBW system disabled. Cancel button pressed.");
  }
}

void DbwNode::overrideBrake(bool override)
{
  bool en = enabled();
  if (override && en) {
    enable_ = false;
  }
  override_brake_ = override;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_WARN("DBW system disabled. Driver override on brake/Accelerator Pedal pedal.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::overrideAcceleratorPedal(bool override)
{
  bool en = enabled();
  if (override && en) {
    enable_ = false;
  }
  override_accelerator_pedal_ = override;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_WARN("DBW system disabled. Driver override on brake/Accelerator Pedal pedal.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::overrideSteering(bool override)
{
  bool en = enabled();
  if (override && en) {
    enable_ = false;
  }
  override_steering_ = override;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_WARN("DBW system disabled. Driver override on steering wheel.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::overrideGear(bool override)
{
  bool en = enabled();
  if (override && en) {
    enable_ = false;
  }
  override_gear_ = override;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_WARN("DBW system disabled. Driver override on shifter.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::timeoutBrake(bool timeout, bool enabled)
{
  if (!timeout_brakes_ && enabled_brakes_ && timeout && !enabled) {
    ROS_WARN("Brake subsystem disabled after 100ms command timeout");
  }
  timeout_brakes_ = timeout;
  enabled_brakes_ = enabled;
}

void DbwNode::timeoutAcceleratorPedal(bool timeout, bool enabled)
{
  if (!timeout_accelerator_pedal_ && enabled_accelerator_pedal_ && timeout && !enabled) {
    ROS_WARN("Accelerator Pedal subsystem disabled after 100ms command timeout");
  }
  timeout_accelerator_pedal_ = timeout;
  enabled_accelerator_pedal_ = enabled;
}

void DbwNode::timeoutSteering(bool timeout, bool enabled)
{
  if (!timeout_steering_ && enabled_steering_ && timeout && !enabled) {
    ROS_WARN("Steering subsystem disabled after 100ms command timeout");
  }
  timeout_steering_ = timeout;
  enabled_steering_ = enabled;
}

void DbwNode::faultBrakes(bool fault)
{
  bool en = enabled();
  if (fault && en) {
    enable_ = false;
  }
  fault_brakes_ = fault;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_ERROR("DBW system disabled. Braking fault.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::faultAcceleratorPedal(bool fault)
{
  bool en = enabled();
  if (fault && en) {
    enable_ = false;
  }
  fault_accelerator_pedal_ = fault;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_ERROR("DBW system disabled. Accelerator Pedal fault.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::faultSteering(bool fault)
{
  bool en = enabled();
  if (fault && en) {
    enable_ = false;
  }
  fault_steering_ = fault;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_ERROR("DBW system disabled. Steering fault.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::faultSteeringCal(bool fault)
{
  bool en = enabled();
  if (fault && en) {
    enable_ = false;
  }
  fault_steering_cal_ = fault;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_ERROR("DBW system disabled. Steering calibration fault.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
}

void DbwNode::faultWatchdog(bool fault, uint8_t src, bool braking)
{
  bool en = enabled();
  if (fault && en) {
    enable_ = false;
  }
  fault_watchdog_ = fault;
  if (publishDbwEnabled()) {
    if (en) {
      ROS_ERROR("DBW system disabled. Watchdog fault.");
    } else {
      ROS_INFO("DBW system enabled.");
    }
  }
  if (braking && !fault_watchdog_using_brakes_) {
    ROS_WARN("Watchdog event: Alerting driver and applying brakes.");
  } else if (!braking && fault_watchdog_using_brakes_) {
    ROS_INFO("Watchdog event: Driver has successfully taken control.");
  }
  if (fault && src && !fault_watchdog_warned_) {
      switch (src) {
        case dbw_pacifica_msgs::WatchdogStatus::OTHER_BRAKE:
          ROS_WARN("Watchdog event: Fault determined by brake controller");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::OTHER_ACCELERATOR_PEDAL:
          ROS_WARN("Watchdog event: Fault determined by Accelerator Pedal controller");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::OTHER_STEERING:
          ROS_WARN("Watchdog event: Fault determined by steering controller");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::BRAKE_COUNTER:
          ROS_WARN("Watchdog event: Brake command counter failed to increment");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::BRAKE_DISABLED:
          ROS_WARN("Watchdog event: Brake transition to disabled while in gear or moving");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::BRAKE_COMMAND:
          ROS_WARN("Watchdog event: Brake command timeout after 100ms");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::BRAKE_REPORT:
          ROS_WARN("Watchdog event: Brake report timeout after 100ms");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::ACCELERATOR_PEDAL_COUNTER:
          ROS_WARN("Watchdog event: Accelerator Pedal command counter failed to increment");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::ACCELERATOR_PEDAL_DISABLED:
          ROS_WARN("Watchdog event: Accelerator Pedal transition to disabled while in gear or moving");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::ACCELERATOR_PEDAL_COMMAND:
          ROS_WARN("Watchdog event: Accelerator Pedal command timeout after 100ms");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::ACCELERATOR_PEDAL_REPORT:
          ROS_WARN("Watchdog event: Accelerator Pedal report timeout after 100ms");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::STEERING_COUNTER:
          ROS_WARN("Watchdog event: Steering command counter failed to increment");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::STEERING_DISABLED:
          ROS_WARN("Watchdog event: Steering transition to disabled while in gear or moving");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::STEERING_COMMAND:
          ROS_WARN("Watchdog event: Steering command timeout after 100ms");
          break;
        case dbw_pacifica_msgs::WatchdogStatus::STEERING_REPORT:
          ROS_WARN("Watchdog event: Steering report timeout after 100ms");
          break;
      }
      fault_watchdog_warned_ = true;
  } else if (!fault) {
    fault_watchdog_warned_ = false;
  }
  fault_watchdog_using_brakes_ = braking;
  if (fault && !fault_watchdog_using_brakes_ && fault_watchdog_warned_) {
    ROS_WARN_THROTTLE(2.0, "Watchdog event: Press left OK button on the steering wheel or cycle power to clear event.");
  }
}

void DbwNode::faultWatchdog(bool fault, uint8_t src) {
  faultWatchdog(fault, src, fault_watchdog_using_brakes_); // No change to 'using brakes' status
}

void DbwNode::publishJointStates(const ros::Time &stamp, const dbw_pacifica_msgs::WheelSpeedReport *wheels, const dbw_pacifica_msgs::SteeringReport *steering)
{
  double dt = (stamp - joint_state_.header.stamp).toSec();
  if (wheels) {
    joint_state_.velocity[JOINT_FL] = wheels->front_left;
    joint_state_.velocity[JOINT_FR] = wheels->front_right;
    joint_state_.velocity[JOINT_RL] = wheels->rear_left;
    joint_state_.velocity[JOINT_RR] = wheels->rear_right;
  }
  if (steering) {
    const double L = acker_wheelbase_;
    const double W = acker_track_;
    const double r = L / tan(steering->steering_wheel_angle / steering_ratio_);
    joint_state_.position[JOINT_SL] = atan(L / (r - W/2));
    joint_state_.position[JOINT_SR] = atan(L / (r + W/2));
  }
  if (dt < 0.5) {
    for (unsigned int i = JOINT_FL; i <= JOINT_RR; i++) {
      joint_state_.position[i] = fmod(joint_state_.position[i] + dt * joint_state_.velocity[i], 2*M_PI);
    }
  }
  joint_state_.header.stamp = stamp;
  pub_joint_states_.publish(joint_state_);
}

} // dbw_pacifica_can