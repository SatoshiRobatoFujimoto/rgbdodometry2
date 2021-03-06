/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#define SHOW_DEBUG_IMAGES 0
#include <opencv2/opencv.hpp>
#include <opencv_lib.hpp>
#include <cstdio>
#include <iostream>
#include <ctime>
#include <ppl.h>

#if defined(HAVE_EIGEN) && EIGEN_WORLD_VERSION == 3
#  ifdef ANDROID
template <typename Scalar> Scalar log2(Scalar v) { using std::log; return log(v) / log(Scalar(2)); }
#  endif
#  if defined __GNUC__ && defined __APPLE__
#    pragma GCC diagnostic ignored "-Wshadow"
#  endif
#  include <unsupported/Eigen/MatrixFunctions>
#  include <Eigen/Dense>
#endif


using namespace cv;
using namespace std;

/*inline */static
void xcomputeC_RigidBodyMotion(double* C, double dIdx, double dIdy, const Point3f& p3d, double fx, double fy)
{
	double invz = 1. / p3d.z,
		v0 = dIdx * fx * invz,
		v1 = dIdy * fy * invz,
		v2 = -(v0 * p3d.x + v1 * p3d.y) * invz;

	C[0] = -p3d.z * v1 + p3d.y * v2;
	C[1] = p3d.z * v0 - p3d.x * v2;
	C[2] = -p3d.y * v0 + p3d.x * v1;
	C[3] = v0;
	C[4] = v1;
	C[5] = v2;
}

/*inline */static
void xcomputeC_Rotation(double* C, double dIdx, double dIdy, const Point3f& p3d, double fx, double fy)
{
	double invz = 1. / p3d.z,
		v0 = dIdx * fx * invz,
		v1 = dIdy * fy * invz,
		v2 = -(v0 * p3d.x + v1 * p3d.y) * invz;

	C[0] = -p3d.z * v1 + p3d.y * v2;
	C[1] = p3d.z * v0 - p3d.x * v2;
	C[2] = -p3d.y * v0 + p3d.x * v1;
}

/*inline */static
void xcomputeC_Translation(double* C, double dIdx, double dIdy, const Point3f& p3d, double fx, double fy)
{
	double invz = 1. / p3d.z,
		v0 = dIdx * fx * invz,
		v1 = dIdy * fy * invz,
		v2 = -(v0 * p3d.x + v1 * p3d.y) * invz;

	C[0] = v0;
	C[1] = v1;
	C[2] = v2;
}

/*inline */static
void xcomputeProjectiveMatrix(const Mat& ksi, Mat& Rt)
{
	CV_Assert(ksi.size() == Size(1, 6) && ksi.type() == CV_64FC1);

#if defined(HAVE_EIGEN) && EIGEN_WORLD_VERSION == 3 && (!defined _MSC_VER || !defined _M_X64 || _MSC_VER > 1500)
	const double* ksi_ptr = reinterpret_cast<const double*>(ksi.ptr(0));
	Eigen::Matrix<double, 4, 4> twist, g;
	twist << 0., -ksi_ptr[2], ksi_ptr[1], ksi_ptr[3],
		ksi_ptr[2], 0., -ksi_ptr[0], ksi_ptr[4],
		-ksi_ptr[1], ksi_ptr[0], 0, ksi_ptr[5],
		0., 0., 0., 0.;
	g = twist.exp();


	eigen2cv(g, Rt);
#else
	// for infinitesimal transformation
	Rt = Mat::eye(4, 4, CV_64FC1);

	Mat R = Rt(Rect(0, 0, 3, 3));
	Mat rvec = ksi.rowRange(0, 3);

	Rodrigues(rvec, R);

	Rt.at<double>(0, 3) = ksi.at<double>(3);
	Rt.at<double>(1, 3) = ksi.at<double>(4);
	Rt.at<double>(2, 3) = ksi.at<double>(5);
#endif
}

static
void pplCvtDepth2Cloud(const Mat& depth, Mat& cloud, const Mat& cameraMatrix)
{
//	CV_Assert(cameraMatrix.type() == CV_64FC1);
	const double inv_fx = 1.f / cameraMatrix.at<double>(0, 0);
	const double inv_fy = 1.f / cameraMatrix.at<double>(1, 1);
	const double ox = cameraMatrix.at<double>(0, 2);
	const double oy = cameraMatrix.at<double>(1, 2);
	cloud.create(depth.size(), CV_32FC3);
	
	Concurrency::parallel_for(0, cloud.rows, [&](int y)
	{
		Point3f* cloud_ptr = reinterpret_cast<Point3f*>(cloud.ptr(y));
		const float* depth_prt = reinterpret_cast<const float*>(depth.ptr(y));
		for (int x = 0; x < cloud.cols; x++)
		{
			float z = depth_prt[x];
			cloud_ptr[x].x = (x - ox) * z * inv_fx;
			cloud_ptr[x].y = (y - oy) * z * inv_fy;
			cloud_ptr[x].z = z;
		}
	});
}

static
void pplCvtDepth2Cloud2(const Mat& depth, Mat& cloud, const Mat& cameraMatrix)
{
	//	CV_Assert(cameraMatrix.type() == CV_64FC1);
	const float inv_fx = 1.f / cameraMatrix.at<float>(0, 0);
	const float inv_fy = 1.f / cameraMatrix.at<float>(1, 1);
	const float ox = cameraMatrix.at<float>(0, 2);
	const float oy = cameraMatrix.at<float>(1, 2);
	cloud.create(depth.size(), CV_32FC3);
	//for (int y = 0; y < cloud.rows; y++)
	Concurrency::parallel_for(0, cloud.rows, [&](int y)
	{
		Point3f* cloud_ptr = reinterpret_cast<Point3f*>(cloud.ptr(y));
		const float* depth_prt = reinterpret_cast<const float*>(depth.ptr(y));
		for (int x = 0; x < cloud.cols; x++)
		{
			float z = depth_prt[x];
			cloud_ptr[x].x = (x - ox) * z * inv_fx;
			cloud_ptr[x].y = (y - oy) * z * inv_fy;
			cloud_ptr[x].z = z;
		}
	});
}


template<class ImageElemType>
static void pplWarpImage(const Mat& image, const Mat& depth,
	const Mat& Rt, const Mat& cameraMatrix, const Mat& distCoeff,
	Mat& warpedImage)
{
	const Rect rect = Rect(0, 0, image.cols, image.rows);

	vector<Point2f> points2d;
	Mat cloud, transformedCloud;

	pplCvtDepth2Cloud(depth, cloud, cameraMatrix);
	perspectiveTransform(cloud, transformedCloud, Rt);

	projectPoints(transformedCloud.reshape(3, 1), Mat::eye(3, 3, CV_64FC1), Mat::zeros(3, 1, CV_64FC1), cameraMatrix, distCoeff, points2d);

	Mat pointsPositions(points2d);
	pointsPositions = pointsPositions.reshape(2, image.rows);

	warpedImage.create(image.size(), image.type());
	warpedImage = Scalar::all(0);

	Mat zBuffer(image.size(), CV_32FC1, FLT_MAX);
	//for (int y = 0; y < image.rows; y++)
	Concurrency::parallel_for(0, image.rows, [&](int y)
	{
		for (int x = 0; x < image.cols; x++)
		{
			const Point3f p3d = transformedCloud.at<Point3f>(y, x);
			const Point p2d = pointsPositions.at<Point2f>(y, x);
			if (!cvIsNaN(cloud.at<Point3f>(y, x).z) && cloud.at<Point3f>(y, x).z > 0 &&
				rect.contains(p2d) && zBuffer.at<float>(p2d) > p3d.z)
			{
				xwarpedImage.at<ImageElemType>(p2d) = image.at<ImageElemType>(y, x);
				zBuffer.at<float>(p2d) = p3d.z;
			}
		}
	});
}

template<class ImageElemType>
static void pplWarpImage2(const Mat& image, const Mat& depth,
	const Mat& Rt, const Mat& cameraMatrix, const Mat& distCoeff,
	Mat& warpedImage)
{
	const Rect rect = Rect(0, 0, image.cols, image.rows);

	vector<Point2f> points2d;
	Mat cloud, transformedCloud;

	pplCvtDepth2Cloud2(depth, cloud, cameraMatrix);
	perspectiveTransform(cloud, transformedCloud, Rt);

	projectPoints(transformedCloud.reshape(3, 1), Mat::eye(3, 3, CV_64FC1), Mat::zeros(3, 1, CV_64FC1), cameraMatrix, distCoeff, points2d);

	Mat pointsPositions(points2d);
	pointsPositions = pointsPositions.reshape(2, image.rows);

	warpedImage.create(image.size(), image.type());
	warpedImage = Scalar::all(0);

	Mat zBuffer(image.size(), CV_32FC1, FLT_MAX);
	//for (int y = 0; y < image.rows; y++)
	Concurrency::parallel_for(0, image.rows, [&](int y)
	{
		for (int x = 0; x < image.cols; x++)
		{
			const Point3f p3d = transformedCloud.at<Point3f>(y, x);
			const Point p2d = pointsPositions.at<Point2f>(y, x);
			if (!cvIsNaN(cloud.at<Point3f>(y, x).z) && cloud.at<Point3f>(y, x).z > 0 &&
				rect.contains(p2d) && zBuffer.at<float>(p2d) > p3d.z)
			{
				warpedImage.at<ImageElemType>(p2d) = image.at<ImageElemType>(y, x);
				zBuffer.at<float>(p2d) = p3d.z;
			}
		}
	});
}

static/* inline*/
void xset2shorts(int& dst, int short_v1, int short_v2)
{
	unsigned short* ptr = reinterpret_cast<unsigned short*>(&dst);
	ptr[0] = static_cast<unsigned short>(short_v1);
	ptr[1] = static_cast<unsigned short>(short_v2);
}

static/* inline*/
void xget2shorts(int src, int& short_v1, int& short_v2)
{
	typedef union { int vint32; unsigned short vuint16[2]; } s32tou16;
	const unsigned short* ptr = (reinterpret_cast<s32tou16*>(&src))->vuint16;
	short_v1 = ptr[0];
	short_v2 = ptr[1];
}

static
int xcomputeCorresp(const Mat& K, const Mat& K_inv, const Mat& Rt,
const Mat& depth0, const Mat& depth1, const Mat& texturedMask1, float maxDepthDiff,
Mat& corresps)
{
	CV_Assert(K.type() == CV_64FC1);
	CV_Assert(K_inv.type() == CV_64FC1);
	CV_Assert(Rt.type() == CV_64FC1);

	corresps.create(depth1.size(), CV_32SC1);

	Mat R = Rt(Rect(0, 0, 3, 3)).clone();

	Mat KRK_inv = K * R * K_inv;
	const double * KRK_inv_ptr = reinterpret_cast<const double *>(KRK_inv.ptr());

	Mat Kt = Rt(Rect(3, 0, 1, 3)).clone();
	Kt = K * Kt;
	const double * Kt_ptr = reinterpret_cast<const double *>(Kt.ptr());

	Rect r(0, 0, depth1.cols, depth1.rows);

	corresps = Scalar(-1);
	int correspCount = 0;

	for (int v1 = 0; v1 < depth1.rows; v1++)
	{
		for (int u1 = 0; u1 < depth1.cols; u1++)
		{
			float d1 = depth1.at<float>(v1, u1);
			if (!cvIsNaN(d1) && texturedMask1.at<uchar>(v1, u1))
			{
				float transformed_d1 = (float)(d1 * (KRK_inv_ptr[6] * u1 + KRK_inv_ptr[7] * v1 + KRK_inv_ptr[8]) + Kt_ptr[2]);
				int u0 = cvRound((d1 * (KRK_inv_ptr[0] * u1 + KRK_inv_ptr[1] * v1 + KRK_inv_ptr[2]) + Kt_ptr[0]) / transformed_d1);
				int v0 = cvRound((d1 * (KRK_inv_ptr[3] * u1 + KRK_inv_ptr[4] * v1 + KRK_inv_ptr[5]) + Kt_ptr[1]) / transformed_d1);

				if (r.contains(Point(u0, v0)))
				{
					float d0 = depth0.at<float>(v0, u0);
					if (!cvIsNaN(d0) && std::abs(transformed_d1 - d0) <= maxDepthDiff)
					{
						int c = corresps.at<int>(v0, u0);
						if (c != -1)
						{
							int exist_u1, exist_v1;
							xget2shorts(c, exist_u1, exist_v1);

							float exist_d1 = (float)(depth1.at<float>(exist_v1, exist_u1) * (KRK_inv_ptr[6] * exist_u1 + KRK_inv_ptr[7] * exist_v1 + KRK_inv_ptr[8]) + Kt_ptr[2]);

							if (transformed_d1 > exist_d1)
								continue;
						}
						else
							correspCount++;

						xset2shorts(corresps.at<int>(v0, u0), u1, v1);
					}
				}
			}
		}
	}

	return correspCount;
}

static/* inline*/
void pplPreprocessDepth(Mat depth0, Mat depth1,
const Mat& validMask0, const Mat& validMask1,
float minDepth, float maxDepth)
{
	CV_DbgAssert(depth0.size() == depth1.size());

	//for (int y = 0; y < depth0.rows; y++)
	Concurrency::parallel_for(0, depth0.rows, [&](int y)
	{
		for (int x = 0; x < depth0.cols; x++)
		{
			float& d0 = depth0.at<float>(y, x);
			if (!cvIsNaN(d0) && (d0 > maxDepth || d0 < minDepth || d0 <= 0 || (!validMask0.empty() && !validMask0.at<uchar>(y, x))))
				d0 = std::numeric_limits<float>::quiet_NaN();

			float& d1 = depth1.at<float>(y, x);
			if (!cvIsNaN(d1) && (d1 > maxDepth || d1 < minDepth || d1 <= 0 || (!validMask1.empty() && !validMask1.at<uchar>(y, x))))
				d1 = std::numeric_limits<float>::quiet_NaN();
		}
	});
}

static
void pplBuildPyramids(const Mat& image0, const Mat& image1,
const Mat& depth0, const Mat& depth1,
const Mat& cameraMatrix, int sobelSize, double sobelScale,
const vector<float>& minGradMagnitudes,
vector<Mat>& pyramidImage0, vector<Mat>& pyramidDepth0,
vector<Mat>& pyramidImage1, vector<Mat>& pyramidDepth1,
vector<Mat>& pyramid_dI_dx1, vector<Mat>& pyramid_dI_dy1,
vector<Mat>& pyramidTexturedMask1, vector<Mat>& pyramidCameraMatrix)
{
	const int pyramidMaxLevel = (int)minGradMagnitudes.size() - 1;

	buildPyramid(image0, pyramidImage0, pyramidMaxLevel);
	buildPyramid(image1, pyramidImage1, pyramidMaxLevel);

	pyramid_dI_dx1.resize(pyramidImage1.size());
	pyramid_dI_dy1.resize(pyramidImage1.size());
	pyramidTexturedMask1.resize(pyramidImage1.size());

	pyramidCameraMatrix.reserve(pyramidImage1.size());

	Mat cameraMatrix_dbl;
	cameraMatrix.convertTo(cameraMatrix_dbl, CV_64FC1);

	for (size_t i = 0; i < pyramidImage1.size(); i++)
	{
		Sobel(pyramidImage1[i], pyramid_dI_dx1[i], CV_16S, 1, 0, sobelSize);
		Sobel(pyramidImage1[i], pyramid_dI_dy1[i], CV_16S, 0, 1, sobelSize);

		const Mat& dx = pyramid_dI_dx1[i];
		const Mat& dy = pyramid_dI_dy1[i];

		Mat texturedMask(dx.size(), CV_8UC1, Scalar(0));
		const float minScalesGradMagnitude2 = (float)((minGradMagnitudes[i] * minGradMagnitudes[i]) / (sobelScale * sobelScale));
		//for (int y = 0; y < dx.rows; y++)
		Concurrency::parallel_for(0, dx.rows, [&](int y)
		{
			for (int x = 0; x < dx.cols; x++)
			{
				float m2 = (float)(dx.at<short>(y, x)*dx.at<short>(y, x) + dy.at<short>(y, x)*dy.at<short>(y, x));
				if (m2 >= minScalesGradMagnitude2)
					texturedMask.at<uchar>(y, x) = 255;
			}
		});
		pyramidTexturedMask1[i] = texturedMask;
		Mat levelCameraMatrix = i == 0 ? cameraMatrix_dbl : 0.5f * pyramidCameraMatrix[i - 1];
		levelCameraMatrix.at<double>(2, 2) = 1.;
		pyramidCameraMatrix.push_back(levelCameraMatrix);
	}

	buildPyramid(depth0, pyramidDepth0, pyramidMaxLevel);
	buildPyramid(depth1, pyramidDepth1, pyramidMaxLevel);
}

static
bool xsolveSystem(const Mat& C, const Mat& dI_dt, double detThreshold, Mat& ksi)
{
#if defined(HAVE_EIGEN) && EIGEN_WORLD_VERSION == 3
	Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> eC, eCt, edI_dt;
	cv2eigen(C, eC);
	cv2eigen(dI_dt, edI_dt);
	eCt = eC.transpose();

	Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> A, B, eksi;

	A = eCt * eC;
	double det = A.determinant();
	if (fabs(det) < detThreshold || cvIsNaN(det) || cvIsInf(det))
		return false;

	B = -eCt * edI_dt;

	eksi = A.ldlt().solve(B);
	eigen2cv(eksi, ksi);

#else
	Mat A = C.t() * C;

	double det = cv::determinant(A);
	if (fabs(det) < detThreshold || cvIsNaN(det) || cvIsInf(det))
		return false;

	Mat B = -C.t() * dI_dt;
	cv::solve(A, B, ksi, DECOMP_CHOLESKY);
#endif

	return true;
}

typedef void(*xComputeCFuncPtr)(double* C, double dIdx, double dIdy, const Point3f& p3d, double fx, double fy);

/*
*  Estimate the rigid body motion from frame0 to frame1. The method is based on the paper
*  "Real-Time Visual Odometry from Dense RGB-D Images", F. Steinbucker, J. Strum, D. Cremers, ICCV, 2011.
*/
//enum {
//	ROTATION2 = 1,
//	TRANSLATION2 = 2,
//	RIGID_BODY_MOTION2 = 4
//};


static
bool xcomputeKsi(int transformType,
const Mat& image0, const Mat&  cloud0,
const Mat& image1, const Mat& dI_dx1, const Mat& dI_dy1,
const Mat& corresps, int correspsCount,
double fx, double fy, double sobelScale, double determinantThreshold,
Mat& ksi)
{
	int Cwidth = -1;
	xComputeCFuncPtr computeCFuncPtr = 0;
	if (transformType == RIGID_BODY_MOTION)
	{
		Cwidth = 6;
		computeCFuncPtr = xcomputeC_RigidBodyMotion;
	}
	else if (transformType == ROTATION)
	{
		Cwidth = 3;
		computeCFuncPtr = xcomputeC_Rotation;
	}
	else if (transformType == TRANSLATION)
	{
		Cwidth = 3;
		computeCFuncPtr = xcomputeC_Translation;
	}
	else
	{
		CV_Error(CV_StsBadFlag, "Unsupported value of transformation type flag.");
	}

	Mat C(correspsCount, Cwidth, CV_64FC1);
	Mat dI_dt(correspsCount, 1, CV_64FC1);

	double sigma = 0;
	int pointCount = 0;
	for (int v0 = 0; v0 < corresps.rows; v0++)
	{
		for (int u0 = 0; u0 < corresps.cols; u0++)
		{
			if (corresps.at<int>(v0, u0) != -1)
			{
				int u1, v1;
				xget2shorts(corresps.at<int>(v0, u0), u1, v1);
				double diff = static_cast<double>(image1.at<uchar>(v1, u1)) -
					static_cast<double>(image0.at<uchar>(v0, u0));
				sigma += diff * diff;
				pointCount++;
			}
		}
	}
	sigma = std::sqrt(sigma / pointCount);
	pointCount = 0;
	for (int v0 = 0; v0 < corresps.rows; v0++)
	{
		for (int u0 = 0; u0 < corresps.cols; u0++)
		{
			if (corresps.at<int>(v0, u0) != -1)
			{
				int u1, v1;
				xget2shorts(corresps.at<int>(v0, u0), u1, v1);

				double diff = static_cast<double>(image1.at<uchar>(v1, u1)) -
					static_cast<double>(image0.at<uchar>(v0, u0));
				double w = sigma + std::abs(diff);
				w = w > DBL_EPSILON ? 1. / w : 1.;

				(*computeCFuncPtr)((double*)C.ptr(pointCount),
					w * sobelScale * dI_dx1.at<short int>(v1, u1),
					w * sobelScale * dI_dy1.at<short int>(v1, u1),
					cloud0.at<Point3f>(v0, u0), fx, fy);
				dI_dt.at<double>(pointCount) = w * diff;
				pointCount++;
			}
		}
	}

	Mat sln;
	bool solutionExist = xsolveSystem(C, dI_dt, determinantThreshold, sln);
	if (solutionExist)
	{
		ksi.create(6, 1, CV_64FC1);
		ksi = Scalar(0);

		Mat subksi;
		if (transformType == RIGID_BODY_MOTION)
		{
			subksi = ksi;
		}
		else if (transformType == ROTATION)
		{
			subksi = ksi.rowRange(0, 3);
		}
		else if (transformType == TRANSLATION)
		{
			subksi = ksi.rowRange(3, 6);
		}

		sln.copyTo(subksi);
	}

	return solutionExist;
}

bool RGBDOdometry2(cv::Mat& Rt, const Mat& initRt,
	const cv::Mat& image0, const cv::Mat& _depth0, const cv::Mat& validMask0,
	const cv::Mat& image1, const cv::Mat& _depth1, const cv::Mat& validMask1,
	const cv::Mat& cameraMatrix, float minDepth, float maxDepth, float maxDepthDiff,
	const std::vector<int>& iterCounts, const std::vector<float>& minGradientMagnitudes,
	int transformType)
{
	const int sobelSize = 3;
	const double sobelScale = 1. / 8;

	Mat depth0 = _depth0.clone(),
		depth1 = _depth1.clone();

	// check RGB-D input data
	CV_Assert(!image0.empty());
	CV_Assert(image0.type() == CV_8UC1);
	CV_Assert(depth0.type() == CV_32FC1 && depth0.size() == image0.size());

	CV_Assert(image1.size() == image0.size());
	CV_Assert(image1.type() == CV_8UC1);
	CV_Assert(depth1.type() == CV_32FC1 && depth1.size() == image0.size());

	// check masks
	CV_Assert(validMask0.empty() || (validMask0.type() == CV_8UC1 && validMask0.size() == image0.size()));
	CV_Assert(validMask1.empty() || (validMask1.type() == CV_8UC1 && validMask1.size() == image0.size()));

	// check camera params
	CV_Assert(cameraMatrix.type() == CV_32FC1 && cameraMatrix.size() == Size(3, 3));

	// other checks
	CV_Assert(iterCounts.empty() || minGradientMagnitudes.empty() ||
		minGradientMagnitudes.size() == iterCounts.size());
	CV_Assert(initRt.empty() || (initRt.type() == CV_64FC1 && initRt.size() == Size(4, 4)));

	vector<int> defaultIterCounts;
	vector<float> defaultMinGradMagnitudes;
	vector<int> const* iterCountsPtr = &iterCounts;
	vector<float> const* minGradientMagnitudesPtr = &minGradientMagnitudes;

	if (iterCounts.empty() || minGradientMagnitudes.empty())
	{
		defaultIterCounts.resize(4);
		defaultIterCounts[0] = 7;
		defaultIterCounts[1] = 7;
		defaultIterCounts[2] = 7;
		defaultIterCounts[3] = 10;

		defaultMinGradMagnitudes.resize(4);
		defaultMinGradMagnitudes[0] = 12;
		defaultMinGradMagnitudes[1] = 5;
		defaultMinGradMagnitudes[2] = 3;
		defaultMinGradMagnitudes[3] = 1;

		iterCountsPtr = &defaultIterCounts;
		minGradientMagnitudesPtr = &defaultMinGradMagnitudes;
	}

	pplPreprocessDepth(depth0, depth1, validMask0, validMask1, minDepth, maxDepth);

	vector<Mat> pyramidImage0, pyramidDepth0,
		pyramidImage1, pyramidDepth1, pyramid_dI_dx1, pyramid_dI_dy1, pyramidTexturedMask1,
		pyramidCameraMatrix;
	pplBuildPyramids(image0, image1, depth0, depth1, cameraMatrix, sobelSize, sobelScale, *minGradientMagnitudesPtr,
		pyramidImage0, pyramidDepth0, pyramidImage1, pyramidDepth1,
		pyramid_dI_dx1, pyramid_dI_dy1, pyramidTexturedMask1, pyramidCameraMatrix);

	Mat resultRt = initRt.empty() ? Mat::eye(4, 4, CV_64FC1) : initRt.clone();
	Mat currRt, ksi;
	for (int level = (int)iterCountsPtr->size() - 1; level >= 0; level--)
	{
		const Mat& levelCameraMatrix = pyramidCameraMatrix[level];

		const Mat& levelImage0 = pyramidImage0[level];
		const Mat& levelDepth0 = pyramidDepth0[level];
		Mat levelCloud0;
		pplCvtDepth2Cloud(pyramidDepth0[level], levelCloud0, levelCameraMatrix);

		const Mat& levelImage1 = pyramidImage1[level];
		const Mat& levelDepth1 = pyramidDepth1[level];
		const Mat& level_dI_dx1 = pyramid_dI_dx1[level];
		const Mat& level_dI_dy1 = pyramid_dI_dy1[level];

		CV_Assert(level_dI_dx1.type() == CV_16S);
		CV_Assert(level_dI_dy1.type() == CV_16S);

		const double fx = levelCameraMatrix.at<double>(0, 0);
		const double fy = levelCameraMatrix.at<double>(1, 1);
		const double determinantThreshold = 1e-6;

		Mat corresps(levelImage0.size(), levelImage0.type());

		// Run transformation search on current level iteratively.
		for (int iter = 0; iter < (*iterCountsPtr)[level]; iter++)
		{
			int correspsCount = xcomputeCorresp(levelCameraMatrix, levelCameraMatrix.inv(), resultRt.inv(DECOMP_SVD),
				levelDepth0, levelDepth1, pyramidTexturedMask1[level], maxDepthDiff,
				corresps);

			if (correspsCount == 0)
				break;

			bool solutionExist = xcomputeKsi(transformType,
				levelImage0, levelCloud0,
				levelImage1, level_dI_dx1, level_dI_dy1,
				corresps, correspsCount,
				fx, fy, sobelScale, determinantThreshold,
				ksi);

			if (!solutionExist)
				break;

			xcomputeProjectiveMatrix(ksi, currRt);

			resultRt = currRt * resultRt;

#if SHOW_DEBUG_IMAGES
			std::cout << "currRt " << currRt << std::endl;
			Mat warpedImage0;
			const Mat distCoeff(1, 5, CV_32FC1, Scalar(0));
			warpImage<uchar>(levelImage0, levelDepth0, resultRt, levelCameraMatrix, distCoeff, warpedImage0);

			imshow("im0", levelImage0);
			imshow("wim0", warpedImage0);
			imshow("im1", levelImage1);
			waitKey();
#endif
		}
	}

	Rt = resultRt;

	return !Rt.empty();
}

int main(int argc, char** argv){

	cv::setUseOptimized(true);

	float vals[] = { 525., 0., 3.1950000000000000e+02,
		0., 525., 2.3950000000000000e+02,
		0., 0., 1. };

	const Mat cameraMatrix = Mat(3, 3, CV_32FC1, vals);
	const Mat distCoeff(1, 5, CV_32FC1, Scalar(0));

	if (argc != 5 && argc != 6)
	{
		cout << "Format: image0 depth0 image1 depth1 [transformationType]" << endl;
		cout << "Depth file must be 16U image stored depth in mm." << endl;
		cout << "Transformation types:" << endl;
		cout << "   -rbm - rigid body motion (default)" << endl;
		cout << "   -r   - rotation rotation only" << endl;
		cout << "   -t   - translation only" << endl;
		return -1;
	}

	Mat colorImage0 = imread(argv[1]);
	Mat depth0 = imread(argv[2], -1);

	Mat colorImage1 = imread(argv[3]);
	Mat depth1 = imread(argv[4], -1);

	if (colorImage0.empty() || depth0.empty() || colorImage1.empty() || depth1.empty())
	{
		cout << "Data (rgb or depth images) is empty.";
		return -1;
	}

	int transformationType = RIGID_BODY_MOTION;
	if (argc == 6)
	{
		string ttype = argv[5];
		if (ttype == "-rbm")
		{
			transformationType = RIGID_BODY_MOTION;
		}
		else if (ttype == "-r")
		{
			transformationType = ROTATION;
		}
		else if (ttype == "-t")
		{
			transformationType = TRANSLATION;
		}
		else
		{
			cout << "Unsupported transformation type." << endl;
			return -1;
		}
	}

	Mat grayImage0, grayImage1, depthFlt0, depthFlt1/*in meters*/;
	cvtColor(colorImage0, grayImage0, COLOR_BGR2GRAY);
	cvtColor(colorImage1, grayImage1, COLOR_BGR2GRAY);
	depth0.convertTo(depthFlt0, CV_32FC1, 1. / 1000);
	depth1.convertTo(depthFlt1, CV_32FC1, 1. / 1000);

	TickMeter tm;
	Mat Rt;

	vector<int> iterCounts(4);
	iterCounts[0] = 7;
	iterCounts[1] = 7;
	iterCounts[2] = 7;
	iterCounts[3] = 10;

	vector<float> minGradMagnitudes(4);
	minGradMagnitudes[0] = 12;
	minGradMagnitudes[1] = 5;
	minGradMagnitudes[2] = 3;
	minGradMagnitudes[3] = 1;

	const float minDepth = 0.f; //in meters
	const float maxDepth = 4.f; //in meters
	const float maxDepthDiff = 0.07f; //in meters

	tm.start();
	bool isFound = RGBDOdometry2(Rt, Mat(),
		grayImage0, depthFlt0, Mat(),
		grayImage1, depthFlt1, Mat(),
		cameraMatrix, minDepth, maxDepth, maxDepthDiff,
		iterCounts, minGradMagnitudes, transformationType);
	tm.stop();

	cout << "Rt = " << Rt << endl;
	cout << "Time = " << tm.getTimeSec() << " sec." << endl;
	//cout << "Time = " << tm.getTimeMilli() << " ms." << endl;

	if (!isFound)
	{
		cout << "Rigid body motion cann't be estimated for given RGBD data." << endl;
		return -1;
	}

	Mat warpedImage0;
	pplWarpImage2<Point3_<uchar> >(colorImage0, depthFlt0, Rt, cameraMatrix, distCoeff, warpedImage0);

	imshow("image0", colorImage0);
	imshow("warped_image0", warpedImage0);
	imshow("image1", colorImage1);
	waitKey();

	return 0;
}