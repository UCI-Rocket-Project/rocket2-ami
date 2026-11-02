#pragma once
// Convert degrees to radians
double Deg2Rad(double deg);

// Convert radians to degrees
double Rad2Deg(double rad);
// Convert GPS coordinates (latitude, longitude, altitude) to ECEF coordinates
void Gps2Ecef(double lat, double lon, double alt, double& x, double& y, double& z);

// Convert NED (North-East-Down) velocity to ECEF velocity
void Ned2EcefVel(double lat, double lon, double alt, double vn, double ve, double vd, double& vx, double& vy, double& vz);
