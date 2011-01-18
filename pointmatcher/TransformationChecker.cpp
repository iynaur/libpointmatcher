// kate: replace-tabs off; indent-width 4; indent-mode normal
// vim: ts=4:sw=4:noexpandtab

#include "Core.h"

using namespace std;

template<typename T>
typename MetricSpaceAligner<T>::Vector MetricSpaceAligner<T>::TransformationChecker::matrixToAngles(const TransformationParameters& parameters)
{
	Vector angles;
	if(parameters.rows() == 4)
	{
		angles = Vector::Zero(3);

		angles(0) = atan2(parameters(2,0), parameters(2,1));
		angles(1) = acos(parameters(2,2));
		angles(2) = -atan2(parameters(0,2), parameters(1,2));
	}
	else
	{
		angles = Vector::Zero(1);

		angles(0) = acos(parameters(0,0));
	}

	return angles;
}

//--------------------------------------
// max iteration counter
template<typename T>
MetricSpaceAligner<T>::CounterTransformationChecker::CounterTransformationChecker(const int maxIterationCount)
{
	this->limits.setZero(1);
	this->limits(0) = maxIterationCount;

	this->valueNames.push_back("Iteration");
	this->limitNames.push_back("Max iteration");
}

template<typename T>
void MetricSpaceAligner<T>::CounterTransformationChecker::init(const TransformationParameters& parameters, bool& iterate)
{
	this->values.setZero(1);
}

template<typename T>
void MetricSpaceAligner<T>::CounterTransformationChecker::check(const TransformationParameters& parameters, bool& iterate)
{
	this->values(0)++;
	
	std::cout << "Iter: " << this->values(0) << " / " << this->limits(0) << std::endl;
	//cerr << parameters << endl;
	
	if (this->values(0) >= this->limits(0))
		iterate = false;
}

template struct MetricSpaceAligner<float>::CounterTransformationChecker;
template struct MetricSpaceAligner<double>::CounterTransformationChecker;


//--------------------------------------
// error
template<typename T>
MetricSpaceAligner<T>::ErrorTransformationChecker::ErrorTransformationChecker(const T minDeltaRotErr, T minDeltaTransErr, const unsigned int tail):
	tail(tail)
{
	this->limits.setZero(4);
	this->limits(0) = minDeltaRotErr;
	this->limits(1) = minDeltaTransErr;
	this->limits(2) = -minDeltaRotErr;
	this->limits(3) = -minDeltaTransErr;
	
	this->valueNames.push_back("Mean abs delta translation err");
	this->valueNames.push_back("Mean abs delta rotation err");
	this->valueNames.push_back("Mean delta translation err");
	this->valueNames.push_back("Mean delta rotation err");
	this->limitNames.push_back("Min delta translation err");
	this->limitNames.push_back("Min delta rotation err");

}

template<typename T>
void MetricSpaceAligner<T>::ErrorTransformationChecker::init(const TransformationParameters& parameters, bool& iterate)
{
	this->values.setZero(4);
	
	rotations.clear();
	translations.clear();
	
	if (parameters.rows() == 4)
	{
		rotations.push_back(Quaternion(Eigen::Matrix<T,3,3>(parameters.corner(Eigen::TopLeft,3,3))));
	}
	else
	{
		// Handle the 2D case
		Eigen::Matrix<T,3,3> m(Matrix::Identity(3,3));
		m.corner(Eigen::TopLeft,2,2) = parameters.corner(Eigen::TopLeft,2,2);
		rotations.push_back(Quaternion(m));
	}
	
	translations.push_back(parameters.corner(Eigen::TopRight,parameters.rows()-1,1));
}

template<typename T>
void MetricSpaceAligner<T>::ErrorTransformationChecker::check(const TransformationParameters& parameters, bool& iterate)
{
	rotations.push_back(Quaternion(Eigen::Matrix<T,3,3>(parameters.corner(Eigen::TopLeft,3,3))));
	translations.push_back(parameters.corner(Eigen::TopRight,parameters.rows()-1,1));
	
	this->values.setZero(4);
	if(rotations.size() > tail)
	{
		for(size_t i = rotations.size()-1; i >= rotations.size()-tail; i--)
		{
			this->values(0) += anyabs(rotations[i].angularDistance(rotations[i-1]));
			this->values(1) += anyabs((translations[i] - translations[i-1]).norm());
			this->values(2) += rotations[i].angularDistance(rotations[i-1]);
			this->values(3) += (translations[i] - translations[i-1]).norm();
		}

		this->values /= tail;

		if(this->values(0) < this->limits(0) && this->values(1) < this->limits(1))
			iterate = false;
	}
	
	std::cout << "Abs Rotation: " << this->values(0) << " / " << this->limits(0) << std::endl;
	std::cout << "Abs Translation: " << this->values(1) << " / " << this->limits(1) << std::endl;
	std::cout << "Rotation: " << this->values(2) << std::endl;
	std::cout << "Translation: " << this->values(3) << std::endl;
	
	if (isnan(this->values(0)))
		throw ConvergenceError("abs rotation norm not a number");
	if (isnan(this->values(1)))
		throw ConvergenceError("abs translation norm not a number");
	if (this->values(2) < this->limits(2) && this->values(3) < this->limits(3))
		throw ConvergenceError("error is increasing");
}

template struct MetricSpaceAligner<float>::ErrorTransformationChecker;
template struct MetricSpaceAligner<double>::ErrorTransformationChecker;

//--------------------------------------
// bound

template<typename T>
MetricSpaceAligner<T>::BoundTransformationChecker::BoundTransformationChecker(const T maxRotationNorm, const T maxTranslationNorm)
{
	this->limits.setZero(2);
	this->limits(0) = maxRotationNorm;
	this->limits(1) = maxTranslationNorm;

	this->limitNames.push_back("Max rotation angle");
	this->limitNames.push_back("Max translation norm");
	this->valueNames.push_back("Rotation angle");
	this->valueNames.push_back("Translation norm");
}

template<typename T>
void MetricSpaceAligner<T>::BoundTransformationChecker::init(const TransformationParameters& parameters, bool& iterate)
{
	this->values.setZero(2);
	// FIXME: handle 2D case
	assert(parameters.rows() == 4);
	initialRotation = Quaternion(Eigen::Matrix<T,3,3>(parameters.corner(Eigen::TopLeft,3,3)));
	initialTranslation = parameters.corner(Eigen::TopRight,parameters.rows()-1,1);
}

template<typename T>
void MetricSpaceAligner<T>::BoundTransformationChecker::check(const TransformationParameters& parameters, bool& iterate)
{
	const Quaternion currentRotation = Quaternion(Eigen::Matrix<T,3,3>(parameters.corner(Eigen::TopLeft,3,3)));
	const Vector currentTranslation = parameters.corner(Eigen::TopRight,parameters.rows()-1,1);
	this->values(0) = currentRotation.angularDistance(initialRotation);
	this->values(1) = (currentTranslation - initialTranslation).norm();
	if (this->values(0) > this->limits(0) || this->values(1) > this->limits(1))
	{
		ostringstream oss;
		oss << "limit out of bounds: ";
		oss << "rot: " << this->values(0) << "/" << this->limits(0) << " ";
		oss << "tr: " << this->values(1) << "/" << this->limits(1);
		throw ConvergenceError(oss.str());
	}
}

template struct MetricSpaceAligner<float>::BoundTransformationChecker;
template struct MetricSpaceAligner<double>::BoundTransformationChecker;
