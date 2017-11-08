#include "io.hpp"
#include <iostream>
#include <iomanip>

constexpr int STRING_WIDTH = 17;

void log(const std::string& message) {
	std::cout << "# " << std::left << std::setw(STRING_WIDTH) << message << std::endl;
}

void log(const std::string& message, double value) {
	std::cout << "# " << std::left << std::setw(STRING_WIDTH) << message << std::left << std::setw(STRING_WIDTH) << value << std::endl;
}

void read_fortran_gauge_field(field<gauge>& U, const std::string& filename) {
	// open file in binary mode
	log(("Reading fortran gauge field from file: " + filename));
	std::ifstream input(filename.c_str(), std::ios::binary);
	// fortran 'unformatted' format: 4-byte delimeter before each variable
	// header consists of 2x doubles and 2x ints
	// so we want to skip 2x doubles + 2x ints + 5x 4-byte delimeters
	constexpr int SKIP_BYTES = 2*8 + 2*4 + 5*4;
	if (input.good()) {
		input.seekg(SKIP_BYTES, std::ios::cur);

		// the rest of the file is now an array of doubles, with ordering
		// [REAL, IMAG]_(0,0) [REAL, IMAG]_(0,1) [REAL, IMAG]_(0,2) [REAL, IMAG]_(1,0) ...
		// so the first 18 doubles make up the matrix [U_mu=t]_(i,j) at (x=0,y=0,z=0,t=0) 
		// followed by another 18 doubles for each mu = x, y, z
		// then this is all repeated for each sites in the order x,y,z,t

		// loop over position indices
		for (int nt=0; nt<U.L0; ++nt) {
			for (int nz=0; nz<U.L3; ++nz) {
				for (int ny=0; ny<U.L2; ++ny) {
					for (int nx=0; nx<U.L1; ++nx) {
						// convert (nx,ny,nz,nt) to ix
						int ix = U.grid.index(nt, nx, ny, nz);
						// loop over directions
						for (int muF=0; muF<4; ++muF) {
							// convert muF=[x,y,z,t] to mu=[t,x,y,z] 
							int mu = (muF+1)%4;
							// loop over elements of a single U_ji matrix
							for (int i=0; i<3; ++i) {
								for (int j=0; j<3; ++j) {
									double tmp_REAL, tmp_IMAG;
									// read complex double
									input.read(reinterpret_cast<char*>(&tmp_REAL), sizeof(tmp_REAL));
									input.read(reinterpret_cast<char*>(&tmp_IMAG), sizeof(tmp_IMAG));
									// assign to corresponding U element
									U[ix][mu](i,j) = std::complex<double> (tmp_REAL, tmp_IMAG);
								}
							}
						}
					}
				}
			}
		}
		log("Gauge field read with plaquette: ", checksum_plaquette(U));
	}
	else {
		log("Failed to open file: " + filename);		
	}
}

std::string make_filename (const std::string& base_name, int config_number) {
	return base_name + "_" + std::to_string(config_number) + ".cnfg";
}

void read_gauge_field (field<gauge>& U, const std::string& base_name, int config_number) {
	std::string filename = make_filename (base_name, config_number);
	std::ifstream input(filename.c_str(), std::ios::binary);
	if (input.good()) {
		double plaq_check;
		// read average plaquette as checksum
		input.read(reinterpret_cast<char*>(&plaq_check), sizeof(plaq_check));
		// read U
		input.read(reinterpret_cast<char*>(&(U[0][0](0,0))), U.V*4*9*sizeof(std::complex<double>));
		// check that plaquette matches checksum
		double plaq = checksum_plaquette(U);
		if(fabs(plaq - plaq_check) > 1.e-15) {
			log("ERROR: read_gauge_field CHECKSUM fail!");
			log("filename: " + filename);
			log("checksum plaquette in file", plaq_check);
			log("measured plaquette", plaq);
			log("deviation", plaq - plaq_check);
			exit(0);
		}
		log("Gauge field [" + filename + "] read with plaquette: ", plaq);
	}
	else {
		log("Failed to read from file: " + filename);		
	}
}

void write_gauge_field (field<gauge>& U, const std::string& base_name, int config_number) {
	std::string filename = make_filename (base_name, config_number);
	std::ofstream output(filename.c_str(), std::ios::binary);
	if (output.good()) {
		double plaq = checksum_plaquette(U);
		// write average plaquette as checksum
		output.write(reinterpret_cast<char*>(&plaq), sizeof(plaq));
		// write U		
		output.write(reinterpret_cast<char*>(&(U[0][0](0,0))), U.V*4*9*sizeof(std::complex<double>));
		log("Gauge field [" + filename + "] written with plaquette: ", plaq);
	}
	else {
		log("Failed to write to file: " + filename);		
	}
}

double checksum_plaquette (const field<gauge> &U) {
	double p = 0;
	#pragma omp parallel for reduction (+:p)
	for(int ix=0; ix<U.V; ++ix) {
		for(int mu=1; mu<4; ++mu) {
			for(int nu=0; nu<mu; nu++) {
				p += ((U[ix][mu]*U.up(ix,mu)[nu])*((U[ix][nu]*U.up(ix,nu)[mu]).adjoint())).trace().real();
			}
		}
	}
	return p / static_cast<double>(3*6*U.V);
}
