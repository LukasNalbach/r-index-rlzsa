// Copyright (c) 2017, Nicola Prezza.  All rights reserved.
// Use of this source code is governed
// by a MIT license that can be found in the LICENSE file.

/*
 * r_index.hpp
 *
 *  Created on: Apr 13, 2017
 *      Author: nico
 *
 * Small version of the r-index: O(r) words of space, O(log(n/r)) locate time per occurrence
 *
 */

#ifndef R_INDEX_S_H_
#define R_INDEX_S_H_

#include <definitions.hpp>
#include <rle_string.hpp>
#include "sparse_sd_vector.hpp"
#include "sparse_hyb_vector.hpp"
#include "utils.hpp"
#include <sdsl/csa_wt.hpp>
#include <sdsl/csa_bitcompressed.hpp>
#include <sdsl/suffix_array_algorithm.hpp>
#include <ips4o.hpp>
#include <sais.hxx>
#include <ankerl/unordered_dense.h>

using namespace sdsl;

namespace ri{

template	<	class sparse_bv_type = sparse_sd_vector,
				class rle_string_t = rle_string_sd
			>
class r_index{

public:

	using triple = std::tuple<range_t, ulint, ulint>;

	r_index(){}

	static constexpr ulint a = 64;

	/*
	 * Build index
	 */
	r_index(string &input, bool sais = true, bool log = false){
		auto time = now();

		this->sais = sais;

		if(contains_reserved_chars(input)){

			if (log) cout << "Error: input string contains one of the reserved characters 0x0, 0x1" << endl;
			exit(1);

		}

		if (log) cout << "n = " << input.size() << endl;

		if (log) cout << "building BWT and computing SA samples";
		if(log && sais) cout << " (SE-SAIS)" << flush;
		else if (log) cout << " (DIVSUFSORT)" << flush;

		//build run-length encoded BWT

		auto bwt_and_sa = sufsort(input);

		string& bwt_s = get<0>(bwt_and_sa);
		int_vector_buffer<>& SA = *get<1>(bwt_and_sa);
		cache_config cc_sa = get<2>(bwt_and_sa);

		if (log) time = log_runtime(time);

		if (log) cout << "RLE encoding BWT" << flush;

		bwt = rle_string_t(bwt_s);

		//build F column
		F = vector<ulint>(256,0);
		for(uchar c : bwt_s)
			F[c]++;

		for(ulint i=255;i>0;--i)
			F[i] = F[i-1];

		F[0] = 0;

		for(ulint i=1;i<256;++i)
			F[i] += F[i-1];

		for(ulint i=0;i<bwt_s.size();++i)
			if(bwt_s[i]==TERMINATOR)
				terminator_position = i;

		assert(input.size()+1 == bwt.size());
		
		if (log) time = log_runtime(time);

		r = bwt.number_of_runs();

		int log_r = bitsize(uint64_t(r));
		int log_n = bitsize(uint64_t(bwt.size()));

		if (log) {
			cout << "Number of BWT equal-letter runs: r = " << r << endl;
			cout << "Rate n/r = " << double(bwt.size())/r << endl;
			cout << "log2(r) = " << log2(double(r)) << endl;
			cout << "log2(n/r) = " << log2(double(bwt.size())/r) << endl;
		}

		if (log) cout << "building SA^d" << flush;

		int_vector<> SA_d_vec;
		ulint bytes_sad;
		double log2_n = log2(2*bwt.size()+1);

		if (log2_n <= 8.0) {
			bytes_sad = 1;
		} else if (log2_n <= 16.0) {
			bytes_sad = 2;
		} else if (log2_n <= 32.0) {
			bytes_sad = 4;
		} else {
			bytes_sad = 8;
		}

		SA_d_vec.width(bytes_sad*8);
		SA_d_vec.resize(bwt.size()+1);
		SA_d_vec[0] = SA[0];

		for (ulint i=1; i<bwt.size(); i++) {
			SA_d_vec[i] = (SA[i]+bwt.size())-SA[i-1];
		}
		
		if (log) time = log_runtime(time);
		if (log) cout << "sorting SA^d" << flush;

		vector<ulint> alphabet; // alphabet of SA^d
		alphabet.resize(bwt.size());

		for (ulint i=0; i<bwt.size(); i++) {
			alphabet[i] = SA_d_vec[i];
		}

		ips4o::sort(alphabet.begin(),alphabet.end()); // sort the values in SA^d
	
		if (log) time = log_runtime(time);
		if (log) cout << "building SA^d alphabet map" << flush;

		auto it = std::unique(alphabet.begin(),alphabet.end()); // remove duplicates in SA^d
		alphabet.resize(std::distance(alphabet.begin(),it));
		ankerl::unordered_dense::map<ulint,ulint> alphabet_map; // map to the effective alphabet of SA^d
		int_vector<> alphabet_map_inv; // map from the effective alphabet of SA^d
		alphabet_map_inv.width(ceil(log2((2*bwt.size()+1))/8.0)*8);
		alphabet_map_inv.resize(alphabet.size()+1);
		alphabet_map_inv[0] = 0;

		{
			ulint j = 1;

			// build alphabet_map and alphabet_map_inv
			for (ulint i=0; i<alphabet.size(); i++) {
				alphabet_map.insert({alphabet[i],j});
				alphabet_map_inv[j] = alphabet[i];
				j++;
			}
		}

		if (log) time = log_runtime(time);
		if (log) cout << "mapping SA^d to its effective alphabet" << flush;

		// map SA^d to its effective alphabet
		for (ulint i=0; i<bwt.size(); i++) {
			SA_d_vec[i] = alphabet_map[SA_d_vec[i]];
		}

		SA_d_vec[bwt.size()] = 0;

		if (log) time = log_runtime(time);
		if (log) cout << "building suffix array of SA^d" << flush;

		vector<int32_t> SA_sad;
		SA_sad.resize(bwt.size()+1);
		
		if (bytes_sad == 1) {
			saisxx(
				reinterpret_cast<uint8_t*>(SA_d_vec.data()),
				SA_sad.begin(),
				(int32_t)bwt.size(),(int32_t)alphabet.size()+1
			);
		} else if (bytes_sad == 2) {
			saisxx(
				reinterpret_cast<uint16_t*>(SA_d_vec.data()),
				SA_sad.begin(),
				(int32_t)bwt.size(),(int32_t)alphabet.size()+1
			);
		} else if (bytes_sad == 4) {
			saisxx(
				reinterpret_cast<uint32_t*>(SA_d_vec.data()),
				SA_sad.begin(),
				(int32_t)bwt.size(),(int32_t)alphabet.size()+1
			);
		} else {
			saisxx(
				reinterpret_cast<uint64_t*>(SA_d_vec.data()),
				SA_sad.begin(),
				(int32_t)bwt.size(),(int32_t)alphabet.size()+1
			);
		}

		if (log) time = log_runtime(time);
		if (log) cout << "building inverse suffix array of SA^d" << flush;

		int_vector<> ISA_sad;
		ISA_sad.width(SA_d_vec.width());
		ISA_sad.resize(bwt.size()+1);

		for (ulint i=0; i<=bwt.size(); i++) {
			ISA_sad[SA_sad[i]] = i;
		}

		if (log) time = log_runtime(time);
		if (log) cout << "counting frequencies of k-mers in SA^d" << flush;

		ulint size_R_target = std::min(std::max<ulint>(1,bwt.size()/3),5*r); // 3'000'000/(SA_d_vec.width()/8)
		ulint segment_size = std::min<ulint>(4069,size_R_target);
		ulint num_segments = bwt.size()/segment_size;
		vector<ulint> selected_segments;
		ulint k = std::min<ulint>(8,size_R_target);
		ulint num_groups;
		
		int_vector<> group;
		int_vector<> freq;
		
		group.width(SA_d_vec.width());
		freq.width(SA_d_vec.width());

		group.resize(bwt.size()+1);
		freq.resize(bwt.size()+1);

		{
			ulint i = 1;
			ulint g = 0;

			while (i <= bwt.size()) {
				ulint j = 1;
				bool equal = true;

				while (equal && i+j <= bwt.size()) {
					for (ulint l=0; l<k; l++) {
						if (
							SA_sad[i+j]+l > bwt.size() ||
							SA_sad[i+j-1]+l > bwt.size() ||
							SA_d_vec[SA_sad[i+j]+l] != SA_d_vec[SA_sad[i+j-1]+l]
						) {
							equal = false;
							break;
						}
					}
					
					if (equal) {
						j++;
					}
				}

				for (ulint p=0; p<j; p++) {
					group[i+p] = g;
					freq[i+p] = j;
				}

				g++;
				i += j;
			}

			num_groups = g;
		}

		if (log) time = log_runtime(time);
		if (log) cout << "counting frequencies of k-mers per segment of SA^d" << flush;

		{
			ulint num_segments_to_select = size_R_target/segment_size;
			std::vector<double> segment_freqs;
			segment_freqs.resize(num_segments+1,0);

			for (ulint s=0; s<num_segments; s++) {
				sdsl::bit_vector is_group_processed;
				is_group_processed.resize(num_groups);

				for (ulint i=s*segment_size; i<(s+1)*segment_size-k; i++) {
					ulint p = ISA_sad[i];

					if (is_group_processed[group[p]] == 0) {
						segment_freqs[s] += std::sqrt((ulint)freq[p]);
						is_group_processed[group[p]] = 1;
					}
				}
			}

			if (log) time = log_runtime(time);
			if (log) cout << "building R" << flush;

			selected_segments.resize(num_segments_to_select);
			sdsl::bit_vector is_segment_selected;
			is_segment_selected.resize(num_segments+1);
			sdsl::bit_vector is_group_processed;
			is_group_processed.resize(num_groups);

			for (ulint s=0; s<num_segments_to_select; s++) {
				ulint best_segment = 0;
				
				for (ulint i=1; i<num_segments; i++) {
					if (is_segment_selected[i] == 0 && segment_freqs[i] > segment_freqs[best_segment]) {
						best_segment = i;
					}
				}
				
				selected_segments[s] = best_segment;
				is_segment_selected[best_segment] = 1;
				segment_freqs[best_segment] = 0;

				for (ulint i=best_segment*segment_size; i<(best_segment+1)*segment_size-k; i++) {
					ulint p = ISA_sad[i];

					if (p <= bwt.size() && is_group_processed[group[p]] == 0) {
						ulint g = group[p];
						ulint freq_sqrt = std::sqrt((ulint)freq[p]);

						while (p > 0 && group[p-1] == g) {
							p--;
						}

						sdsl::bit_vector is_segment_processed;
						is_segment_processed.resize(num_segments+1);
						is_segment_processed[best_segment] = 1;

						do {
							ulint seg = SA_sad[p]/segment_size;

							if (
								SA_sad[p] < (seg+1)*segment_size-k &&
								is_segment_processed[seg] == 0 &&
								is_segment_selected[seg] == 0
							) {
								segment_freqs[seg] -= freq_sqrt;
								is_segment_processed[seg] = 1;
							}
							
							p++;
						} while (p <= bwt.size() && group[p] == g);

						is_group_processed[g] = 1;
					}
				}
			}

			ips4o::sort(selected_segments.begin(),selected_segments.end());
		}

		{
			R.width(ceil(log2((2*bwt.size()+1))/8.0)*8);
			R.resize(segment_size*selected_segments.size());
			ulint j = 0;

			for (ulint i=0; i<selected_segments.size(); i++) {
				ulint segment_start = selected_segments[i]*segment_size;

				for (ulint l=0; l<segment_size; l++) {
					R[j] = alphabet_map_inv[SA_d_vec[segment_start+l]];
					j++;
				}
			}
		}

		if (log) time = log_runtime(time);
		if (log) cout << "building FM-Index for R" << flush;

		int_vector<> R_rev;
		R_rev.width(ceil(log2((2*bwt.size()+1))/8.0)*8);
		R_rev.resize(R.size());

		for (ulint i=0; i<R.size(); i++) {
			R_rev[i] = R[R.size()-i-1];
		}

		sdsl::csa_bitcompressed<sdsl::int_alphabet<>> FM_rrev;
		sdsl::construct_im(FM_rrev,R_rev,0);

		if (log) time = log_runtime(time);
		if (log) cout << "building rlzsa" << flush;

		S.width(ceil(log2((bwt.size()+1))/8.0)*8);
		PS.width(ceil(log2((bwt.size()+1))/8.0)*8);

		S.resize(1);
		PS.resize(1);
		PL.resize(1);
		S[0] = SA[0];
		PS[0] = 0;
		PL[0] = 0;
		ulint z_l = 1;

		{
			ulint i = 1;
			ulint z = 1;
			int_vector<> SA_d_val;
			SA_d_val.resize(1);
			
			// rlz-encode SA^d
			while (i < bwt.size()) {
				ulint b = 0;
				ulint e = FM_rrev.size()-1;
				ulint b_last,e_last,pos_sa;
				ulint l = 0;

				// find the longest prefix SA^d[i,i+d) of SA^d[i,n) that occurs in R
				while (true) {
					b_last = b;
					e_last = e;
					SA_d_val[0] = alphabet_map_inv[SA_d_vec[i+l]];

					if (sdsl::backward_search(FM_rrev, b, e, SA_d_val.begin(), SA_d_val.end(), b, e) == 0) {
						break;
					}

					l++;

					if (l == UINT16_MAX || i+l == bwt.size()) {
						b_last = b;
						e_last = e;
						break;
					}
				}

				z++;
				PL.resize(z);
				PL[z-1] = l;
				S.resize(z);

				if (z % a == 1) {
					PS.resize(PS.size()+1);
					PS[PS.size()-1] = i;
				}

				if (l == 0) {
					// literal phrase
					S[z-1] = SA[i];
					i++;
					z_l++;
				} else {
					// copy-phrase
					S[z-1] = (R.size()-FM_rrev[b_last])-l;
					i += l;

					// add a literal phrase after each copy-phrase
					if (i < bwt.size()) {
						z++;
						PL.resize(z);
						PL[z-1] = 0;
						S.resize(z);
						S[z-1] = SA[i];

						if (z % a == 1) {
							PS.resize(PS.size()+1);
							PS[PS.size()-1] = i;
						}
						
						i++;
						z_l++;
					}
				}
			}

			PL.resize(z+1);
			PL[z] = 1;
		}

		if (log) time = log_runtime(time);
	    sdsl::remove(cache_file_name(conf::KEY_SA, cc_sa));

		if (log) {
			std::cout << std::endl;
			std::cout << "z: " << PL.size() << std::endl;
			std::cout << "z_l/z: " << z_l/(double)PL.size() << std::endl;
			std::cout << "z/r: " << PL.size()/(double)r << std::endl;
		}

		/*
		std::cout << "SA: ";
		for (ulint i=0; i<bwt.size(); i++) {
			std::cout << SA[i] << ", ";
		}

		std::cout << std::endl << "SA^d: ";
		std::cout << SA[0] << ", ";
		for (ulint i=1; i<bwt.size(); i++) {
			std::cout << int64_t{alphabet_map_inv[SA_d_vec[i]]}-int64_t{bwt.size()} << ", ";
		}

		std::cout << std::endl << "R: ";
		for (ulint i=0; i<R.size(); i++) {
			std::cout << int64_t{R[i]}-int64_t{bwt.size()} << ", ";
		}
		
		std::cout << std::endl << "PL: ";
		for (ulint i=0; i<PL.size(); i++) {
			std::cout << PL[i] << ", ";
		}

		std::cout << std::endl << "S: ";
		for (ulint i=0; i<S.size(); i++) {
			std::cout << S[i] << ", ";
		}

		std::cout << std::endl << "PS: ";
		for (ulint i=0; i<PS.size(); i++) {
			std::cout << PS[i] << ", ";
		}
		std::cout << std::endl;
		*/

	}

	/*
	 * get full BWT range
	 */
	range_t full_range(){

		//inclusive range
		return {0,bwt_size()-1};

	}

	uchar operator[](ulint i ){
		return bwt[i];
	}

	/*
	 * \param r inclusive range of a string w
	 * \param c character
	 * \return inclusive range of cw
	 */
	range_t LF(range_t rn, uchar c){

		//if character does not appear in the text, return empty pair
		if((c==255 and F[c]==bwt_size()) || F[c]>=F[c+1])
			return {1,0};

		//number of c before the interval
		ulint c_before = bwt.rank(rn.first,c);

		//number of c inside the interval rn
		ulint c_inside = bwt.rank(rn.second+1,c) - c_before;

		//if there are no c in the interval, return empty range
		if(c_inside==0) return {1,0};

		ulint l = F[c] + c_before;

		return {l,l+c_inside-1};

	}

	//backward navigation of the BWT
	ulint LF(ulint  i){

		auto c = bwt[i];
		return F[c] + bwt.rank(i,c);

	}

	//forward navigation of the BWT
	ulint FL(ulint  i){

		//i-th character in first BWT column
		auto c = F_at(i);

		//this c is the j-th (counting from 0)
		ulint j = i - F[c];

		return bwt.select(j,uchar(c));

	}

	//forward navigation of the BWT, where for efficiency we give c=F[i] as input
	ulint FL(ulint  i, uchar c){

		//i-th character in first BWT column
		assert(c == F_at(i));

		//this c is the j-th (counting from 0)
		ulint j = i - F[c];

		return bwt.select(j,uchar(c));

	}

	/*
	 * access column F at position i
	 */
	uchar F_at(ulint i){

		ulint c = (upper_bound(F.begin(),F.end(),i) - F.begin()) - 1;
		assert(c<256);
		assert(i>=F[c]);

		return uchar(c);

	}

	/*
	 * Return BWT range of character c
	 */
	range_t get_char_range(uchar c){

		//if character does not appear in the text, return empty pair
		if((c==255 and F[c]==bwt_size()) || F[c]>=F[c+1])
			return {1,0};

		ulint l = F[c];
		ulint r = bwt_size()-1;

		if(c<255)
			r = F[c+1]-1;

		return {l,r};

	}

	/*
	 * Return BWT range of pattern P
	 */
	range_t count(string &P){

		auto range = full_range();
		ulint m = P.size();

		for(ulint i=0;i<m and range.second>=range.first;++i)
			range = LF(range,P[m-i-1]);

		return range;

	}

	/*
	 * Return number of occurrences of P in the text
	 */
	ulint occ(string &P){

		auto rn = count(P);

		return rn.second>=rn.first ? (rn.second-rn.first)+1 : 0;

	}

	/*
	 * iterator locate(string &P){
	 *
	 * 	return iterator to iterate over all occurrences without storing them
	 * 	in memory
	 *
	 * }
	 */

	/*
	 * locate all occurrences of P and return them in an array
	 * (space consuming if result is big).
	 */
	vector<ulint> locate_all(string& P){

		vector<ulint> OCC;

		range_t res = count_and_get_occ(P);
		
		ulint l = res.first;
		ulint r = res.second;

		ulint n_occ = r>=l ? (r-l)+1 : 0;

		ulint x_ps; // index in PS of the last phrase starting before or at position l		
		{
			ulint b = 0;
			ulint e = PS.size()-1;
			ulint m;

			while (b != e) {
				m = b+(e-b)/2+1;

				if (PS[m] <= l) {
					b = m;
				} else {
					e = m-1;
				}
			}

			x_ps = b;
		}

		ulint s_cp = PS[x_ps]; // starting position of the current phrase
		ulint x_p = a * x_ps;
		
		// find the phrase containing l
		while (s_cp + std::max<ulint>(1,PL[x_p]) <= l) {
			s_cp += std::max<ulint>(1,PL[x_p]);
			x_p++;
		}

		ulint i; // current position in the suffix array
		ulint s; // current suffix

		if (PL[x_p] == 0) {
			// the x_p-th phrase is a literal phrase
			i = l;
		} else {
			// the x_p-th phrase is a copy phrase, hence the x_p-1-th phrase is a
			// literal phrase, because there is a literal phrase before each copy-phrase;
			// so store its SA-value in s and decode the x_p-th phrase up to position l

			i = s_cp;
			s = S[x_p-1];

			ulint p_r = S[x_p]; // current position in R

			while (i < l) {
				s += R[p_r];
				s -= bwt.size();
				p_r++;
				i++;
			}
		}

		OCC.reserve(n_occ);
		ulint s_np = s_cp + std::max<ulint>(1,PL[x_p]); // starting position of the next phrase
	
		while (true) {
			// decode all literal phrases until the next copy phrase or until i > r
			while (PL[x_p] == 0 && i <= r) {
				s = S[x_p];
				OCC.emplace_back(s);
				x_p++;
				s_cp++;
				s_np += std::max<ulint>(1,PL[x_p]);
				i++;
			}

			if (i > r) {
				break;
			}

			ulint p_r = S[x_p] + (i - s_cp); // current position in R

			// decode the copy phrase and stop as soon as i > r
			do {
				s += R[p_r];
				s -= bwt.size();
				OCC.emplace_back(s);
				p_r++;
				i++;
			} while (i < s_np && i <= r);

			if (i > r) {
				break;
			}

			x_p++;
			s_cp = s_np;
			s_np += std::max<ulint>(1,PL[x_p]);
		}

		return OCC;

	}


	/*
	 * get number of runs in the BWT (terminator character included)
	 */
	ulint number_of_runs(){
		return bwt.number_of_runs();
	}

	/*
	 * get terminator (0x1) position in the BWT
	 */
	ulint get_terminator_position(){
		return terminator_position;
	}

	/*
	 * get BWT in string format
	 */
	string get_bwt(){
		return bwt.toString();
	}

	/* serialize the structure to the ostream
	 * \param out	 the ostream
	 */
	ulint serialize(std::ostream& out){

		ulint w_bytes = 0;

		assert(F.size()>0);
		assert(bwt.size()>0);

		out.write((char*)&terminator_position,sizeof(terminator_position));
		out.write((char*)F.data(),256*sizeof(ulint));

		w_bytes += sizeof(terminator_position) + 256*sizeof(ulint);

		w_bytes += bwt.serialize(out);

		w_bytes += R.serialize(out);
		w_bytes += PS.serialize(out);
		w_bytes += PL.serialize(out);
		w_bytes += S.serialize(out);

		return w_bytes;

	}

	/* load the structure from the istream
	 * \param in the istream
	 */
	void load(std::istream& in) {

		in.read((char*)&terminator_position,sizeof(terminator_position));

		F = vector<ulint>(256);
		in.read((char*)F.data(),256*sizeof(ulint));

		bwt.load(in);

		r = bwt.number_of_runs();

		R.load(in);
		PS.load(in);
		PL.load(in);
		S.load(in);

	}

	/*
	 * save the structure to the path specified.
	 * \param path_prefix prefix of the index files. suffix ".ri" will be automatically added
	 */
	void save_to_file(string path_prefix){

		string path = string(path_prefix).append(".ri");

		std::ofstream out(path);
		serialize(out);
		out.close();

	}

	/*
	 * load the structure from the path specified.
	 * \param path: full file name
	 */
	void load_from_file(string path){

		std::ifstream in(path);
		load(in);
		in.close();

	}

	ulint text_size(){
		return bwt.size()-1;
	}

	ulint bwt_size(){
		return bwt.size();
	}

	uchar get_terminator(){
		return TERMINATOR;
	}

	ulint print_space(){

		cout << "Number of runs = " << bwt.number_of_runs() << endl<<endl;

		ulint tot_bytes = bwt.print_space();

		cout << "\nTOT BWT space: " << tot_bytes << " Bytes" <<endl<<endl;

		return tot_bytes;

	}
	
    uint64_t size_in_bytes() {
        uint64_t size = 0;

		size += 256*sizeof(ulint);
		size += bwt.size_in_bytes();
		size += sdsl::size_in_bytes(R);
		size += sdsl::size_in_bytes(PS);
		size += sdsl::size_in_bytes(PL);
		size += sdsl::size_in_bytes(S);            

        return size;
    }

    void log_data_structure_sizes() {
        std::cout << "index size: " << format_size(size_in_bytes()) << std::endl;

        std::cout << "bwt: " << format_size(bwt.size_in_bytes()) << std::endl;
        std::cout << "R: " << format_size(sdsl::size_in_bytes(R)) << std::endl;
        std::cout << "PS: " << format_size(sdsl::size_in_bytes(PS)) << std::endl;
        std::cout << "PL: " << format_size(sdsl::size_in_bytes(PL)) << std::endl;
        std::cout << "S: " << format_size(sdsl::size_in_bytes(S)) << std::endl;
    }

private:

	/*
	 * returns <l,r>, where l,r are the inclusive ranges of the pattern P. If P does not occur, then l>r
	 *
	 * returns range
	 *
	 */
	range_t count_and_get_occ(string &P){

		range_t range = full_range();

		range_t range1;

		ulint m = P.size();

		for(ulint i=0;i<m and range.second>=range.first;++i){

			uchar c = P[m-i-1];

			range1 = LF(range,c);

			range = range1;

		}

		return range;

	}

	/*
	 * returns a triple containing BWT of input string
	 * (uses 0x1 character as terminator), text positions corresponding
	 * to first letters in BWT runs (plus their ranks from 0 to R-1), and text positions corresponding
	 * to last letters in BWT runs (in BWT order)
	 */
	tuple<string, int_vector_buffer<>*, cache_config> sufsort(string &s){

		string bwt_s;

	    cache_config cc;

	    int_vector<8> text(s.size());
	    assert(text.size()==s.size());

	    for(ulint i=0;i<s.size();++i)
	    	text[i] = (uchar)s[i];

	    assert(text.size()==s.size());

	    append_zero_symbol(text);

	    store_to_cache(text, conf::KEY_TEXT, cc);

	    construct_config::byte_algo_sa = sais ? SE_SAIS : LIBDIVSUFSORT;
	    construct_sa<8>(cc);

	    //now build BWT from SA
	    int_vector_buffer<>* sa = new int_vector_buffer<>(cache_file_name(conf::KEY_SA, cc));

		{

	        for (ulint i=0; i<sa->size(); i++){
	            auto x = (*sa)[i];

	            assert(x<=text.size());

	            if ( x > 0 )
	            	bwt_s.push_back((uchar)text[x-1]);
	            else
	            	bwt_s.push_back(TERMINATOR);
	        }

	    }

	    sdsl::remove(cache_file_name(conf::KEY_TEXT, cc));
	    //sdsl::remove(cache_file_name(conf::KEY_SA, cc));

	    return tuple<string, int_vector_buffer<>*, cache_config>(bwt_s, sa, cc);

	}

	static bool contains_reserved_chars(string &s){

		for(auto c : s)
			if(c == 0 or c == 1)
				return true;

		return false;

	}

	static const uchar TERMINATOR = 1;

	bool sais = true;

	/*
	 * sparse RLBWT: r (log sigma + (1+epsilon) * log (n/r)) (1+o(1)) bits
	 */

	//F column of the BWT (vector of 256 elements)
	vector<ulint> F;
	//L column of the BWT, run-length compressed
	rle_string_t bwt;
	ulint terminator_position = 0;
	ulint r = 0;//number of BWT runs

	sdsl::int_vector<> R; // rlzsa reference
	sdsl::int_vector<> PS; // rlzsa phrase starting positions (every a-th phrase is sampled)
	sdsl::int_vector<16> PL; // rlzsa phrase lengths
	sdsl::int_vector<> S; // starting positions of the rlzsa phrases in R
};

}

#endif /* R_INDEX_S_H_ */