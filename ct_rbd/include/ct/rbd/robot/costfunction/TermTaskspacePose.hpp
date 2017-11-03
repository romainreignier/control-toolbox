/**********************************************************************************************************************
This file is part of the Control Toobox (https://adrlab.bitbucket.io/ct), copyright by ETH Zurich, Google Inc.
Authors:  Michael Neunert, Markus Giftthaler, Markus Stäuble, Diego Pardo, Farbod Farshidian
Licensed under Apache2 license (see LICENSE file in main directory)
**********************************************************************************************************************/

#pragma once

#include <ct/optcon/costfunction/term/TermBase.hpp>
#include <ct/optcon/costfunction/utility/utilities.hpp>

#include <iit/rbd/traits/TraitSelector.h>
#include <ct/rbd/robot/Kinematics.h>
#include <ct/rbd/state/RBDState.h>


namespace ct {
namespace rbd {


/*!
 * \brief A costfunction term that defines a cost on a task-space pose
 *
 *  @todo add velocity to term
 *
 * \tparam KINEMATICS kinematics of the system
 * \tparam FB true if system is a floating base robot
 * \tparam STATE_DIM state dimensionality of the system
 * \tparam CONTROL_DIM control dimensionality of the system
 *
 */
template <class KINEMATICS, bool FB, size_t STATE_DIM, size_t CONTROL_DIM>
class TermTaskspacePose : public optcon::TermBase<STATE_DIM, CONTROL_DIM, double, ct::core::ADCGScalar>
{
public:
    using SCALAR = ct::core::ADCGScalar;

    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    //! the trivial constructor is explicitly forbidden
    TermTaskspacePose() {}
    //! constructor using a quaternion for orientation
    TermTaskspacePose(size_t eeInd,
        const Eigen::Matrix<double, 3, 3>& Qpos,
        const double& Qrot,
        const core::StateVector<3, double>& w_pos_des,
        const Eigen::Quaternion<double>& w_q_des,
        const std::string& name = "TermTaskSpace")
        : optcon::TermBase<STATE_DIM, CONTROL_DIM, double, ct::core::ADCGScalar>(name),
          eeInd_(eeInd),
          Q_pos_(Qpos),
          Q_rot_(Qrot),
          w_p_ref_(w_pos_des),
          w_R_ref_(w_q_des.normalized().toRotationMatrix())
    {
        // Checks whether STATE_DIM has the appropriate size.
        //  2 * (FB * 6 + KINEMATICS::NJOINTS)) represents a floating base system with euler angles
        //  2 * (FB * 6 + KINEMATICS::NJOINTS) + 1 representing a floating base system with quaternion angles
        static_assert(
            (STATE_DIM == 2 * (FB * 6 + KINEMATICS::NJOINTS)) || (STATE_DIM == 2 * (FB * 6 + KINEMATICS::NJOINTS) + 1),
            "STATE_DIM does not have appropriate size.");
    }

    //! constructor using euler angles for orientation
    TermTaskspacePose(size_t eeInd,
        const Eigen::Matrix<double, 3, 3>& Qpos,
        const double& Qrot,
        const core::StateVector<3, double>& w_pos_des,
        const Eigen::Matrix<double, 3, 1>& eulerXyz,
        const std::string& name = "TermTaskSpace")
        // delegate constructor
        : TermTaskspacePose(eeInd,
              Qpos,
              Qrot,
              w_pos_des,
              Eigen::Quaternion<double>(Eigen::AngleAxisd(eulerXyz(0), Eigen::Vector3d::UnitX()) *
                                        Eigen::AngleAxisd(eulerXyz(1), Eigen::Vector3d::UnitY()) *
                                        Eigen::AngleAxisd(eulerXyz(2), Eigen::Vector3d::UnitZ())),
              name)
    {
    }


    //! construct this term with info loaded from a configuration file
    TermTaskspacePose(std::string& configFile, const std::string& termName, bool verbose = false)
    {
        loadConfigFile(configFile, termName, verbose);
    }

    //! copy constructor
    TermTaskspacePose(const TermTaskspacePose& arg)
        : eeInd_(arg.eeInd_),
          kinematics_(KINEMATICS()),
          Q_pos_(arg.Q_pos_),
          Q_rot_(arg.Q_rot_),
          w_p_ref_(arg.w_p_ref_),
          w_R_ref_(arg.w_R_ref_)
    {
    }

    //! destructor
    virtual ~TermTaskspacePose() {}
    //! deep cloning
    TermTaskspacePose<KINEMATICS, FB, STATE_DIM, CONTROL_DIM>* clone() const override
    {
        return new TermTaskspacePose(*this);
    }

    //! evaluate
    virtual SCALAR evaluate(const Eigen::Matrix<SCALAR, STATE_DIM, 1>& x,
        const Eigen::Matrix<SCALAR, CONTROL_DIM, 1>& u,
        const SCALAR& t) override
    {
        return evalLocal<SCALAR>(x, u, t);
    }

    //! we overload the evaluateCppadCg method
    virtual ct::core::ADCGScalar evaluateCppadCg(const core::StateVector<STATE_DIM, ct::core::ADCGScalar>& x,
        const core::ControlVector<CONTROL_DIM, ct::core::ADCGScalar>& u,
        ct::core::ADCGScalar t) override
    {
        return evalLocal<ct::core::ADCGScalar>(x, u, t);
    }

    //! load term information from configuration file (stores data in member variables)
    void loadConfigFile(const std::string& filename, const std::string& termName, bool verbose = false) override
    {
        if (verbose)
            std::cout << "Loading TermTaskspacePose from file " << filename << std::endl;

        ct::optcon::loadScalarCF(filename, "eeId", eeInd_, termName);
        ct::optcon::loadScalarCF(filename, "Q_rot", Q_rot_, termName);

        ct::optcon::loadMatrixCF(filename, "Q_pos", Q_pos_, termName);
        ct::optcon::loadMatrixCF(filename, "x_des", w_p_ref_, termName);

        // try loading a quaternion directly
        try
        {
            std::cout << "trying to load quaternion" << std::endl;
            Eigen::Matrix<double, 4, 1> quat_vec;
            ct::optcon::loadMatrixCF(filename, "quat_des", quat_vec, termName);
            Eigen::Quaterniond quat_des(quat_vec(0), quat_vec(1), quat_vec(2), quat_vec(3));
            w_R_ref_ = quat_des.toRotationMatrix();
            if (verbose)
                std::cout << "Read quat_des as = \n"
                          << quat_des.w() << " " << quat_des.x() << " " << quat_des.y() << " " << quat_des.z() << " "
                          << std::endl;
        } catch (const std::exception& e)
        {
            // quaternion load failed, try loading euler angles
            std::cout << "trying to load euler angles" << std::endl;
            try
            {
                Eigen::Vector3d eulerXyz;
                ct::optcon::loadMatrixCF(filename, "eulerXyz_des", eulerXyz, termName);
                Eigen::Quaternion<double> quat_des(Eigen::AngleAxisd(eulerXyz(0), Eigen::Vector3d::UnitX()) *
                                                   Eigen::AngleAxisd(eulerXyz(1), Eigen::Vector3d::UnitY()) *
                                                   Eigen::AngleAxisd(eulerXyz(2), Eigen::Vector3d::UnitZ()));
                w_R_ref_ = quat_des.toRotationMatrix();
                if (verbose)
                    std::cout << "Read desired Euler Angles Xyz as  = \n" << eulerXyz.transpose() << std::endl;
            } catch (const std::exception& e)
            {
                throw std::runtime_error(
                    "Failed to load TermTaskspacePose, could not find a desired end effector orientation in file.");
            }
        }

        if (verbose)
        {
            std::cout << "Read eeId as eeId = \n" << eeInd_ << std::endl;
            std::cout << "Read Q_pos as Q_pos = \n" << Q_pos_ << std::endl;
            std::cout << "Read Q_rot as Q_rot = \n" << Q_rot_ << std::endl;
            std::cout << "Read x_des as x_des = \n" << w_p_ref_.transpose() << std::endl;
        }
    }


private:
    //! a templated evaluate() method
    template <typename SC>
    SC evalLocal(const Eigen::Matrix<SC, STATE_DIM, 1>& x, const Eigen::Matrix<SC, CONTROL_DIM, 1>& u, const SC& t)
    {
        using Matrix3Tpl = Eigen::Matrix<SC, 3, 3>;

        // transform the robot state vector into a CT RBDState
        tpl::RBDState<KINEMATICS::NJOINTS, SC> rbdState = setStateFromVector<FB>(x);

        // position difference in world frame
        Eigen::Matrix<SC, 3, 1> xDiff =
            kinematics_.getEEPositionInWorld(eeInd_, rbdState.basePose(), rbdState.jointPositions())
                .toImplementation() -
            w_p_ref_.template cast<SC>();

        // compute the cost based on the position error
        SC pos_cost = (xDiff.transpose() * Q_pos_.template cast<SC>() * xDiff)(0, 0);


        // get current end-effector rotation in world frame
        Matrix3Tpl ee_rot = kinematics_.getEERotInWorld(eeInd_, rbdState.basePose(), rbdState.jointPositions());

        // compute a measure for the difference between current rotation and desired rotation and compute cost based on the orientation error
        // for the intuition behind, consider the following posts:
        // https://math.stackexchange.com/a/87698
        // https://math.stackexchange.com/a/773635
        Matrix3Tpl ee_rot_diff = w_R_ref_.template cast<SC>().transpose() * ee_rot;

        SC rot_cost = (SC)Q_rot_ * (ee_rot_diff - Matrix3Tpl::Identity()).norm();  // the frobenius norm of (R_diff-I)

        return pos_cost + rot_cost;
    }


    //! computes RBDState in case the user supplied a floating-base robot
    template <bool T>
    tpl::RBDState<KINEMATICS::NJOINTS, SCALAR> setStateFromVector(const Eigen::Matrix<SCALAR, STATE_DIM, 1>& x,
        typename std::enable_if<T, bool>::type = true)
    {
        tpl::RBDState<KINEMATICS::NJOINTS, SCALAR> rbdState;
        rbdState.fromStateVectorEulerXyz(x);

        return rbdState;
    }

    //! computes RBDState in case the user supplied a fixed-base robot
    template <bool T>
    tpl::RBDState<KINEMATICS::NJOINTS, SCALAR> setStateFromVector(const Eigen::Matrix<SCALAR, STATE_DIM, 1>& x,
        typename std::enable_if<!T, bool>::type = true)
    {
        tpl::RBDState<KINEMATICS::NJOINTS, SCALAR> rbdState;
        rbdState.joints() = x;
        return rbdState;
    }

    //! index of the end-effector in question
    size_t eeInd_;

    //! the robot kinematics
    KINEMATICS kinematics_;

    //! weighting matrix for the task-space position
    Eigen::Matrix<double, 3, 3> Q_pos_;

    //! weighting factor for orientation error
    double Q_rot_;

    //! reference position in world frame
    Eigen::Matrix<double, 3, 1> w_p_ref_;

    //! reference ee orientation in world frame
    Eigen::Matrix<double, 3, 3> w_R_ref_;
};


}  // namespace rbd
}  // namespace ct