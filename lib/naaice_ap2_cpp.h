
/**************************************************************************//**
 *
 *    `7MN.   `7MF'     db            db      `7MMF'  .g8"""bgd `7MM"""YMM  
 *      MMN.    M      ;MM:          ;MM:       MM  .dP'     `M   MM    `7  
 *      M YMb   M     ,V^MM.        ,V^MM.      MM  dM'       `   MM   d    
 *      M  `MN. M    ,M  `MM       ,M  `MM      MM  MM            MMmmMM    
 *      M   `MM.M    AbmmmqMA      AbmmmqMA     MM  MM.           MM   Y  , 
 *      M     YMM   A'     VML    A'     VML    MM  `Mb.     ,'   MM     ,M 
 *    .JML.    YM .AMA.   .AMMA..AMA.   .AMMA..JMML.  `"bmmmd'  .JMMmmmmMMM 
 * 
 *  Network-Attached Accelerators for Energy-Efficient Heterogeneous Computing
 * 
 * @file naaice_ap2.h
 *
 * @brief C++ interface to NAAICE.
 * 
 * The C++ interface provides a simple and flexible use pattern for offloading
 * work to NAAs. This enables quick prototyping and adoption of NAAICE by 
 * diverse scientific codes.
 *
 * @author Dylan Everingham
 * Contact: everingham@zib.de
 * 
 *****************************************************************************/

#ifndef NAAICE_AP2_H
#define NAAICE_AP2_H

/*
 * Dependencies
 */

#include <naaice_ap2.h>
#include <future>
#include <vector>
#include <concepts>
#include <iostream>

/*
 * Concepts required for template function definitions.
 */

template<typename T>
concept Arithmetic = std::integral<T> or std::floating_point<T>;

template<typename T>
using as_vector = std::vector<
	typename T::value_type,
	typename T::allocator_type>;

template<typename T>
concept Vector = std::same_as<T, as_vector<T>>;

template<typename T>
concept ArithmeticVector = Vector<T> and Arithmetic<typename T::value_type>;


/* 
 * Class NAAHandler
 * 
 * Encapsulates all interaction between the host and the NAA.
 */

class NAAHandler {
private:

	// NAA function code.
	size_t fncode;

	// NAA handle struct for C layer.
	naa_handle handle;

	// NAA status struct for C layer.
	naa_status status;

	template<typename T>
	requires (Arithmetic<T>)
	std::pair<std::vector<naa_param_t>, size_t> handle_naa_params(
		size_t input_amount, std::vector<T>& param);
	template<typename T, typename... Rest>
	requires (Arithmetic<T>)
	std::pair<std::vector<naa_param_t>, size_t> handle_naa_params(
		size_t input_amount, std::vector<T>& param, Rest... rest);

	template<typename outputT, size_t outputSize>
	std::vector<outputT> naa_thread_helper(
		std::vector<naa_param_t> input_params,
		std::vector<naa_param_t> output_params);

	template<typename outputT, size_t outputSize>
	std::future<std::vector<outputT>> launch_naa_thread(
		std::vector<naa_param_t> input_params,
		std::vector<naa_param_t> output_params);

public:

	/**
 	 * Constructor.
 	 * Simply initializes class members.
 	 */
	NAAHandler(size_t fncode) : fncode(fncode) {};

	/**
 	 * Destructor.
 	 * Simply calls NAA_FINALIZE from the C API.
 	 */
	~NAAHandler();

	template<typename outputT, size_t outputSize, typename... inputTs>
	requires ((ArithmeticVector<inputTs> || Arithmetic<inputTs>) && ...)
	std::future<std::vector<outputT>> invoke(inputTs&... args);

	// Single call, i.e. no create. Is it possible to simply only call
	// naa_create on first call for an NAAHandler?

	// Could maybe use this to accpet a wider range of iterable structures.

	// Also might be nice to accept non-iterable (value-type) arguments.
	// And vectors of arbitrary types (bytes, bools...S)

	// Could also be fine to accept a list of vectors.

	// The previous version can be generalized to accept different container types
	// containing vectors.

};

#include "naaice_ap2.tpp"

#endif


// Questions

// - How do we infer / handle the size and type of output parameters?
//		DYL: Should be known by user, validated by RMS.

// - How is information about the input and output data types (crucially, their sizes),
//   passed to the C layer?
//		DYL: We just need the array pointer and total parameter size, not data type.
//		Everything should be converted to void / char pointer when passed to C.
//
//		DYL: But to be a template parameter, they must be known at compile time!