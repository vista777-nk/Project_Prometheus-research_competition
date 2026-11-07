#include "dmx_all.h"

//#define delta_T     0.0001f  //5ms计算一次
//#define M_PI        3.1415926f

//float I_ex, I_ey, I_ez;  // 误差积分

//quater_param_t Q_info = {1, 0, 0};  // 全局四元数
//euler_param_t eulerAngle; //欧拉角

//icm_param_t icm_data;
//gyro_param_t GyroOffset;

//float param_Kp = 0.12;   // 加速度计的收敛速率比例增益 //0.17
//float param_Ki = 0.0028;   //陀螺仪收敛速率的积分增益 0.004


float fast_sqrt(float x) 
{
    float halfx = 0.5f * x;
    float y = x;
    long i = *(long *) &y;
    i = 0x5f3759df - (i >> 1);
    y = *(float *) &i;
    y = y * (1.5f - (halfx * y * y));
    return y;
}


////转化为实际物理值
//void ICM_getValues() {
//    //一阶低通滤波，单位g/s
//    icm_data.acc_x = (((float) icm42688_acc_x) * alpha) * 8 / 4096 + icm_data.acc_x * (1 - alpha);
//    icm_data.acc_y = (((float) icm42688_acc_y) * alpha) * 8 / 4096 + icm_data.acc_y * (1 - alpha);
//    icm_data.acc_z = (((float) icm42688_acc_z) * alpha) * 8 / 4096 + icm_data.acc_z * (1 - alpha);


//    //陀螺仪角度转弧度
//    icm_data.gyro_x = ((float) icm42688_gyro_x - GyroOffset.Xdata) * M_PI / 180 / 16.4f;
//    icm_data.gyro_y = ((float) icm42688_gyro_y - GyroOffset.Ydata) * M_PI / 180 / 16.4f;
//    icm_data.gyro_z = ((float) icm42688_gyro_z - GyroOffset.Zdata) * M_PI / 180 / 16.4f;
//}

#define	pi		3.14159265f                           
#define	Kp		5.12f                        
#define	Ki		0.001f                         
#define	halfT	0.006f           

float q0=1,q1=0,q2=0,q3=0;   
float exInt=0,eyInt=0,ezInt=0;  

float	AngleX=0,AngleY=0,AngleZ=0;					//四元数解算出的欧拉角

void gyroOffset_init(void)      /////////陀螺仪零飘初始化
{
		unsigned char i;
    g_x = 0;
    g_y = 0;
    g_z = 0;
    for (i = 0; i < 100; ++i) {
				// 获取ICM42688陀螺仪角加速度数据
				//Get_Gyro_ICM42688();
				Get_Gyro_ICM42688_IIC();
        g_x += icm42688_gyro_x;
        g_y += icm42688_gyro_y;
        g_z += icm42688_gyro_z;
        delay_ms(10);
    }

    g_x /= 100;
    g_y /= 100;
    g_z /= 100;
}

#define alpha           0.3f

void IMUupdate(float gx, float gy, float gz, float ax, float ay, float az)
{
	float norm;
	float vx, vy, vz;
	float ex, ey, ez;
	
	
    //一阶低通滤波，单位g/s
    ax = (((float) ax) * alpha) * 9.8 / 4096 + ax * (1 - alpha);
    ay = (((float) ay) * alpha) * 9.8 / 4096 + ay * (1 - alpha);
    az = (((float) az) * alpha) * 9.8 / 4096 + az * (1 - alpha);

	//对加速度数据进行归一化,得到单位加速度
	norm = sqrt(ax*ax + ay*ay + az*az);	//把加速度计的三维向量转成单维向量   
	ax = ax / norm;
	ay = ay / norm;
	az = az / norm;

		//	下面是把四元数换算成《方向余弦矩阵》中的第三列的三个元素。 
		//	根据余弦矩阵和欧拉角的定义，地理坐标系的重力向量，转到机体坐标系，正好是这三个元素
		//	所以这里的vx vy vz，其实就是当前的欧拉角（即四元数）的机体坐标参照系上，换算出来的重力单位向量。
	//根据当前四元数的姿态值来估算出各重力分量。用于和加速计实际测量出来的各重力分量进行对比，从而实现对四轴姿态的修正
	vx = 2*(q1*q3 - q0*q2);
	vy = 2*(q0*q1 + q2*q3);
	vz = q0*q0 - q1*q1 - q2*q2 + q3*q3 ;
	//叉积来计算估算的重力和实际测量的重力这两个重力向量之间的误差。
	ex = (ay*vz - az*vy) ;
	ey = (az*vx - ax*vz) ;
	ez = (ax*vy - ay*vx) ;

		//用叉乘误差来做PI修正陀螺零偏，
    //通过调节 param_Kp，param_Ki 两个参数，
    //可以控制加速度计修正陀螺仪积分姿态的速度。
//	exInt = exInt + ex * Ki;
//	eyInt = eyInt + ey * Ki;
//	ezInt = ezInt + ez * Ki;
//	
//	gx = gx + Kp*ex + exInt;
//	gy = gy + Kp*ey + eyInt;
//	gz = gz + Kp*ez + ezInt;
	
    exInt += halfT * ex;
    eyInt += halfT * ey;
    ezInt += halfT * ez;

    gx = gx + Kp * ex + Ki * exInt;
    gy = gy + Kp * ey + Ki * eyInt;
    gz = gz + Kp * ez + Ki * ezInt;

	//四元数微分方程,其中halfT为测量周期的1/2,gx gy gz为陀螺仪角速度,以下都是已知量,这里使用了一阶龙哥库塔求解四元数微分方程
	q0 = q0 + (-q1*gx - q2*gy - q3*gz) * halfT;
	q1 = q1 + ( q0*gx + q2*gz - q3*gy) * halfT;
	q2 = q2 + ( q0*gy - q1*gz + q3*gx) * halfT;
	q3 = q3 + ( q0*gz + q1*gy - q2*gx) * halfT;
	// 归一化四元数
	norm = sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
	q0 = q0 / norm;
	q1 = q1 / norm;
	q2 = q2 / norm;
	q3 = q3 / norm;

	// 四元数计算欧拉角,换算成度,180 / PI = 57.2957795;
	AngleX = asin(2*(q0*q2 - q1*q3 )) * 57.2957795f; // 俯仰   
	AngleY = asin(2*(q0*q1 + q2*q3 )) * 57.2957795f; // 横滚
	//AngleZ = asin(2*(q0*q3 + q1*q2 )) * 57.2957795f; // 偏航
	
}