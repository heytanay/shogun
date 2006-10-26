/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2006 Soeren Sonnenburg
 * Written (W) 1999-2006 Gunnar Raetsch
 * Copyright (C) 1999-2006 Fraunhofer Institute FIRST and Max-Planck-Society
 */

// HMM.cpp: implementation of the CDynProg class.
// $Id$
//////////////////////////////////////////////////////////////////////

#include "structure/DynProg.h"
#include "lib/Mathematics.h"
#include "lib/io.h"
#include "lib/config.h"
#include "features/StringFeatures.h"
#include "features/CharFeatures.h"
#include "features/Alphabet.h"
#include "structure/Plif.h"
#include "lib/DynArray2.h"
#include "lib/DynArray3.h"
#include "lib/fibheap.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#ifdef SUNOS
extern "C" int	finite(double);
#endif

#define USEHEAP 0
#define USEORIGINALLIST 0
#define USEFIXEDLENLIST 2

const INT CDynProg::num_degrees = 4;
const INT CDynProg::num_svms = 8 ;

//static const INT word_degree[num_degrees] = {1,6} ;
//static const INT cum_num_words[num_degrees+1] = {0,4,4100} ;
//static const INT num_words[num_degrees] = {4,4096} ;
//static const INT word_degree[num_degrees] = {1,2,3,4,5,6} ;
//static const INT cum_num_words[num_degrees+1] = {0,4,20,84,340,1364,5460} ;
//static const INT num_words[num_degrees] = {4,20,64,256,1024,4096} ;
const INT CDynProg::word_degree[CDynProg::num_degrees] = {3,4,5,6} ;
const INT CDynProg::cum_num_words[CDynProg::num_degrees+1] = {0,64,320,1344,5440} ;
const INT CDynProg::num_words[CDynProg::num_degrees] = {64,256,1024,4096} ;

const INT CDynProg::num_words_single = 4 ;
const INT CDynProg::word_degree_single = 1 ;
const INT CDynProg::num_svms_single = 1 ;
bool CDynProg::word_used_single[CDynProg::num_words_single] ;
DREAL CDynProg::svm_value_unnormalized_single[CDynProg::num_svms_single] ;
CDynamicArray2<DREAL> CDynProg::dict_weights(1,1) ;
INT CDynProg::num_unique_words_single = 0 ;

CDynamicArray2<bool> CDynProg::word_used(CDynProg::num_degrees,4096) ;
CDynamicArray2<DREAL> CDynProg::svm_values_unnormalized(num_degrees,num_svms) ;
INT CDynProg::svm_pos_start[num_degrees] ;
INT CDynProg::num_unique_words[num_degrees] ;

CDynProg::CDynProg()
	: m_seq(1,1), m_pos(1), m_orf_info(1,2), m_plif_list(1), m_PEN(1,1), 
	  m_genestr(1), m_dict_weights(1),
	  transition_matrix_a(1,1), initial_state_distribution_p(1,1), end_state_distribution_q(1,1)
{
	this->N=1;
	m_step=0 ;
}

CDynProg::CDynProg(INT N, double* p, double* q, double* a)
	: m_seq(N,1), m_pos(1), m_orf_info(N,2), m_plif_list(1), m_PEN(N,N), 
	  m_genestr(1), m_dict_weights(1),
	  transition_matrix_a(a,N,N,false), initial_state_distribution_p(p,N,N,false), end_state_distribution_q(q,N,N,false)
{
	this->N=N;
}

CDynProg::CDynProg(INT N, double* p, double* q, int num_trans, double* a_trans)
	: m_seq(N,1), m_pos(1), m_orf_info(N,2), m_plif_list(1), m_PEN(N,N), 
	  m_genestr(1), m_dict_weights(1),
	  transition_matrix_a(N,N), initial_state_distribution_p(p,N,N,false), end_state_distribution_q(q,N,N,false)
{
	this->N=N;
	
	trans_list_forward = NULL ;
	trans_list_forward_cnt = NULL ;
	trans_list_forward_val = NULL ;
	trans_list_backward = NULL ;
	trans_list_backward_cnt = NULL ;
	trans_list_len = 0 ;

	mem_initialized = true ;

	trans_list_forward_cnt=NULL ;
	trans_list_len = N ;
	trans_list_forward = new T_STATES*[N] ;
	trans_list_forward_val = new DREAL*[N] ;
	trans_list_forward_cnt = new T_STATES[N] ;
	
	INT start_idx=0;
	for (INT j=0; j<N; j++)
	{
		INT old_start_idx=start_idx;

		while (start_idx<num_trans && a_trans[start_idx+num_trans]==j)
		{
			start_idx++;
			
			if (start_idx>1 && start_idx<num_trans)
				ASSERT(a_trans[start_idx+num_trans-1] <= a_trans[start_idx+num_trans]);
		}
		
		if (start_idx>1 && start_idx<num_trans)
			ASSERT(a_trans[start_idx+num_trans-1] <= a_trans[start_idx+num_trans]);
		
		INT len=start_idx-old_start_idx;
		ASSERT(len>=0);
		
		trans_list_forward_cnt[j] = 0 ;
		
		if (len>0)
		{
			trans_list_forward[j]     = new T_STATES[len] ;
			trans_list_forward_val[j] = new DREAL[len] ;
		}
		else
		{
			trans_list_forward[j]     = NULL;
			trans_list_forward_val[j] = NULL;
		}
	}
	
	for (INT i=0; i<num_trans; i++)
	{
		INT from = (INT)a_trans[i+num_trans] ;
		INT to   = (INT)a_trans[i] ;
		DREAL val = a_trans[i+num_trans*2] ;
		
		ASSERT(from>=0 && from<N) ;
		ASSERT(to>=0 && to<N) ;
		
		trans_list_forward[from][trans_list_forward_cnt[from]]=to ;
		trans_list_forward_val[from][trans_list_forward_cnt[from]]=val ;
		trans_list_forward_cnt[from]++ ;
		//ASSERT(trans_list_forward_cnt[from]<3000) ;
	} ;
}


CDynProg::~CDynProg()
{
	if (trans_list_forward_cnt)
	  delete[] trans_list_forward_cnt ;
	if (trans_list_backward_cnt)
		delete[] trans_list_backward_cnt ;
	if (trans_list_forward)
	{
	    for (INT i=0; i<trans_list_len; i++)
			if (trans_list_forward[i])
				delete[] trans_list_forward[i] ;
	    delete[] trans_list_forward ;
	}
	if (trans_list_forward_val)
	{
	    for (INT i=0; i<trans_list_len; i++)
			if (trans_list_forward_val[i])
				delete[] trans_list_forward_val[i] ;
	    delete[] trans_list_forward_val ;
	}
	if (trans_list_backward)
	  {
	    for (INT i=0; i<trans_list_len; i++)
	      if (trans_list_backward[i])
		delete[] trans_list_backward[i] ;
	    delete[] trans_list_backward ;
	  } ;
}

void CDynProg::set_p(DREAL *p, INT N) 
{
	m_seq(N,1), m_pos(1), m_orf_info(N,2), m_plif_list(1), m_PEN(N,N), 
	  m_genestr(1), m_dict_weights(1),

	initial_state_distribution_p.set_array(p, N, N, true, true) ;
	this->N=N ;
}

void CDynProg::set_q(DREAL *q, INT N) 
{
	end_state_distribution_q.seq_array(q, N, N, true, true) ;
}

void CDynProg::set_a_trans(DREAL *a_trans, INT num_trans) 
{
	trans_list_forward = NULL ;
	trans_list_forward_cnt = NULL ;
	trans_list_forward_val = NULL ;
	trans_list_backward = NULL ;
	trans_list_backward_cnt = NULL ;
	trans_list_len = 0 ;

	mem_initialized = true ;

	trans_list_forward_cnt=NULL ;
	trans_list_len = N ;
	trans_list_forward = new T_STATES*[N] ;
	trans_list_forward_val = new DREAL*[N] ;
	trans_list_forward_cnt = new T_STATES[N] ;
	
	INT start_idx=0;
	for (INT j=0; j<N; j++)
	{
		INT old_start_idx=start_idx;

		while (start_idx<num_trans && a_trans[start_idx+num_trans]==j)
		{
			start_idx++;
			
			if (start_idx>1 && start_idx<num_trans)
				ASSERT(a_trans[start_idx+num_trans-1] <= a_trans[start_idx+num_trans]);
		}
		
		if (start_idx>1 && start_idx<num_trans)
			ASSERT(a_trans[start_idx+num_trans-1] <= a_trans[start_idx+num_trans]);
		
		INT len=start_idx-old_start_idx;
		ASSERT(len>=0);
		
		trans_list_forward_cnt[j] = 0 ;
		
		if (len>0)
		{
			trans_list_forward[j]     = new T_STATES[len] ;
			trans_list_forward_val[j] = new DREAL[len] ;
		}
		else
		{
			trans_list_forward[j]     = NULL;
			trans_list_forward_val[j] = NULL;
		}
	}
	
	for (INT i=0; i<num_trans; i++)
	{
		INT from = (INT)a_trans[i+num_trans] ;
		INT to   = (INT)a_trans[i] ;
		DREAL val = a_trans[i+num_trans*2] ;
		
		ASSERT(from>=0 && from<N) ;
		ASSERT(to>=0 && to<N) ;
		
		trans_list_forward[from][trans_list_forward_cnt[from]]=to ;
		trans_list_forward_val[from][trans_list_forward_cnt[from]]=val ;
		trans_list_forward_cnt[from]++ ;
		//ASSERT(trans_list_forward_cnt[from]<3000) ;
	} ;

}

void CDynProg::best_path_set_seq(DREAL *seq, INT N, INT seq_len) 
{
	m_seq.set_array(seq, N, seq_len, true, true) ;
	m_step=2 ;
}

void CDynProg::best_path_set_pos(INT *pos, INT seq_len)  
{
	if (m_step!=2)
		CIO::message(M_ERROR, "please call best_path_set_seq first\n") ;

	m_pos.set_array(pos, seq_len, seq_len, true, true) ;

	m_step=3 ;
}

void CDynProg::best_path_set_orf_info(INT *orf_info, INT m, INT n) 
{
	if (m_step!=3)
		CIO::message(M_ERROR, "please call best_path_set_pos first\n") ;
		
	m_orf_info.set_array(orf_info, m, n, true, true) ;
	if (m!=N)
		CIO::message(M_ERROR, "orf_info size does not match previous info %i!=%i\n", m, N) ;
	if (n!=2)
		CIO::message(M_ERROR, "orf_info size incorrect %i!=2\n", n) ;
	
	m_step=4 ;
}

void CDynProg::best_path_set_plif_list(CPlif **plif_list, INT num_plif) 
{
	if (m_step!=4)
		CIO::message(M_ERROR, "please call best_path_set_orf_info first\n") ;

	m_plif_list.set_array(plif_list, num_plif, num_plif, true, true) ;

	m_step=5 ;
}

void CDynProg::best_path_set_plif_id_matrix(INT *plif_id_matrix, INT m, INT n) 
{
	if (m_step!=5)
		CIO::message(M_ERROR, "please call best_path_set_plif_list first\n") ;

	m_PEN.set_array(plif_list, num_plif, num_plif, true, true) ;

	m_step=6 ;
}

void CDynProg::best_path_set_genestr(CHAR* genestr, INT genestr_len)
{
	if (m_step!=6)
		CIO::message(M_ERROR, "please call best_path_set_plif_id_matrix first\n") ;

	m_step=7 ;
}

void CDynProg::best_path_set_dict_weights(DREAL* dictionary_weights, INT dict_len) 
{
	if (m_step!=7)
		CIO::message(M_ERROR, "please call best_path_set_genestr first\n") ;

	m_step=7 ;
}


void CDynProg::best_path_call(INT nbest, bool use_orf) 
{
	if (m_step!=8)
		CIO::message(M_ERROR, "please call best_path_set_orf_dict_weights first\n") ;

	m_step=9 ;
}


void CDynProg::best_path_get_score(DREAL **score, INT *N) 
{
	if (m_step!=9)
		CIO::message(M_ERROR, "please call best_path_call first\n") ;

	m_step=10 ;
}

void CDynProg::best_path_get_states(INT **states, INT *N, INT *M) 
{
	if (m_step!=10)
		CIO::message(M_ERROR, "please call best_path_get_score first\n") ;

	m_step=11 ;
}

void CDynProg::best_path_get_positions(INT **positions, INT *N, INT *M) 
{
	if (m_step!=11)
		CIO::message(M_ERROR, "please call best_path_get_positions first\n") ;

	m_step=12 ;
}


DREAL CDynProg::best_path_no_b(INT max_iter, INT &best_iter, INT *my_path)
{
	CDynamicArray2<T_STATES> psi(max_iter, N) ;
	CDynamicArray<DREAL>* delta = new CDynamicArray<DREAL>(N) ;
	CDynamicArray<DREAL>* delta_new = new CDynamicArray<DREAL>(N) ;
	
	{ // initialization
		for (INT i=0; i<N; i++)
		{
			delta->element(i) = get_p(i) ;
			psi.element(0, i)= 0 ;
		}
	} 
	
	DREAL best_iter_prob = CMath::ALMOST_NEG_INFTY ;
	best_iter = 0 ;
	
	// recursion
	for (INT t=1; t<max_iter; t++)
	{
		CDynamicArray<DREAL>* dummy;
		INT NN=N ;
		for (INT j=0; j<NN; j++)
		{
			DREAL maxj = delta->element(0) + transition_matrix_a.element(0,j);
			INT argmax=0;
			
			for (INT i=1; i<NN; i++)
			{
				DREAL temp = delta->element(i) + transition_matrix_a.element(i,j);
				
				if (temp>maxj)
				{
					maxj=temp;
					argmax=i;
				}
			}
			delta_new->element(j)=maxj ;
			psi.element(t, j)=argmax ;
		}
		
		dummy=delta;
		delta=delta_new;
		delta_new=dummy;	//switch delta/delta_new
		
		{ //termination
			DREAL maxj=delta->element(0)+get_q(0);
			INT argmax=0;
			
			for (INT i=1; i<N; i++)
			{
				DREAL temp=delta->element(i)+get_q(i);
				
				if (temp>maxj)
				{
					maxj=temp;
					argmax=i;
				}
			}
			//pat_prob=maxj;
			
			if (maxj>best_iter_prob)
			{
				my_path[t]=argmax;
				best_iter=t ;
				best_iter_prob = maxj ;
			} ;
		} ;
	}

	
	{ //state sequence backtracking
		for (INT t = best_iter; t>0; t--)
		{
			my_path[t-1]=psi.element(t, my_path[t]);
		}
	}

	delete delta ;
	delete delta_new ;
	
	return best_iter_prob ;
}

void CDynProg::best_path_no_b_trans(INT max_iter, INT &max_best_iter, short int nbest, DREAL *prob_nbest, INT *my_paths)
{
	//T_STATES *psi=new T_STATES[max_iter*N*nbest] ;
	CDynamicArray3<T_STATES> psi(max_iter,N,nbest) ;
	CDynamicArray3<short int> ktable(max_iter,N,nbest) ;
	CDynamicArray2<short int> ktable_ends(max_iter,nbest) ;

	CDynamicArray<DREAL> tempvv(nbest*N) ;
	CDynamicArray<INT> tempii(nbest*N) ;

	CDynamicArray2<T_STATES> path_ends(max_iter,nbest) ;
	CDynamicArray2<DREAL> *delta=new CDynamicArray2<DREAL>(N,nbest) ;
	CDynamicArray2<DREAL> *delta_new=new CDynamicArray2<DREAL>(N,nbest) ;
	CDynamicArray2<DREAL> delta_end(max_iter,nbest) ;

	CDynamicArray2<INT> paths(max_iter,nbest) ;
	paths.set_array(my_paths, max_iter*nbest, max_iter*nbest) ;

	{ // initialization
		for (T_STATES i=0; i<N; i++)
		{
			delta->element(i,0) = get_p(i) ;
			for (short int k=1; k<nbest; k++)
			{
				delta->element(i,k)=-CMath::INFTY ;
				ktable.element(0,i,k)=0 ;
			}
		}
	}
	
	// recursion
	for (INT t=1; t<max_iter; t++)
	{
		CDynamicArray2<DREAL>* dummy=NULL;

		for (T_STATES j=0; j<N; j++)
		{
			const T_STATES num_elem   = trans_list_forward_cnt[j] ;
			const T_STATES *elem_list = trans_list_forward[j] ;
			const DREAL *elem_val = trans_list_forward_val[j] ;
			
			INT list_len=0 ;
			for (short int diff=0; diff<nbest; diff++)
			{
				for (INT i=0; i<num_elem; i++)
				{
					T_STATES ii = elem_list[i] ;
					
					tempvv.element(list_len) = -(delta->element(ii,diff) + elem_val[i]) ;
					tempii.element(list_len) = diff*N + ii ;
					list_len++ ;
				}
			}
			CMath::qsort(tempvv.get_array(), tempii.get_array(), list_len) ;
			
			for (short int k=0; k<nbest; k++)
			{
				if (k<list_len)
				{
					delta_new->element(j,k)  = -tempvv[k] ;
					psi.element(t,j,k)      = (tempii[k]%N) ;
					ktable.element(t,j,k)   = (tempii[k]-(tempii[k]%N))/N ;
				}
				else
				{
					delta_new->element(j,k)  = -CMath::INFTY ;
					psi.element(t,j,k)      = 0 ;
					ktable.element(t,j,k)   = 0 ;
				}
			}
		}
		
		dummy=delta;
		delta=delta_new;
		delta_new=dummy;	//switch delta/delta_new
		
		{ //termination
			INT list_len = 0 ;
			for (short int diff=0; diff<nbest; diff++)
			{
				for (T_STATES i=0; i<N; i++)
				{
					tempvv.element(list_len) = -(delta->element(i,diff)+get_q(i));
					tempii.element(list_len) = diff*N + i ;
					list_len++ ;
				}
			}
			CMath::qsort(tempvv.get_array(), tempii.get_array(), list_len) ;
			
			for (short int k=0; k<nbest; k++)
			{
				delta_end.element(t-1,k) = -tempvv[k] ;
				path_ends.element(t-1,k) = (tempii[k]%N) ;
				ktable_ends.element(t-1,k) = (tempii[k]-(tempii[k]%N))/N ;
			}
		}
	}
	
	{ //state sequence backtracking
		max_best_iter=0 ;
		
		CDynamicArray<DREAL> sort_delta_end(max_iter*nbest) ;
		CDynamicArray<short int> sort_k(max_iter*nbest) ;
		CDynamicArray<INT> sort_t(max_iter*nbest) ;
		CDynamicArray<INT> sort_idx(max_iter*nbest) ;
		
		INT i=0 ;
		for (INT iter=0; iter<max_iter-1; iter++)
			for (short int k=0; k<nbest; k++)
			{
				sort_delta_end[i]=-delta_end.element(iter,k) ;
				sort_k[i]=k ;
				sort_t[i]=iter+1 ;
				sort_idx[i]=i ;
				i++ ;
			}
		
		CMath::qsort(sort_delta_end.get_array(), sort_idx.get_array(), (max_iter-1)*nbest) ;

		for (short int n=0; n<nbest; n++)
		{
			short int k=sort_k[sort_idx[n]] ;
			INT iter=sort_t[sort_idx[n]] ;
			prob_nbest[n]=-sort_delta_end[n] ;

			if (iter>max_best_iter)
				max_best_iter=iter ;
			
			ASSERT(k<nbest) ;
			ASSERT(iter<max_iter) ;
			
			paths.element(iter,n) = path_ends.element(iter-1, k) ;
			short int q   = ktable_ends.element(iter-1, k) ;
			
			for (INT t = iter; t>0; t--)
			{
				paths.element(t-1,n)=psi.element(t, paths.element(t,n), q);
				q = ktable.element(t, paths.element(t,n), q) ;
			}
		}
	}

	delete delta ;
	delete delta_new ;
}


void CDynProg::translate_from_single_order(WORD* obs, INT sequence_length, 
										   INT start, INT order, 
										   INT max_val)
{
	INT i,j;
	WORD value=0;
	
	for (i=sequence_length-1; i>= ((int) order)-1; i--)	//convert interval of size T
	{
		value=0;
		for (j=i; j>=i-((int) order)+1; j--)
			value= (value >> max_val) | (obs[j] << (max_val * (order-1)));
		
		obs[i]= (WORD) value;
	}
	
	for (i=order-2;i>=0;i--)
	{
		value=0;
		for (j=i; j>=i-order+1; j--)
		{
			value= (value >> max_val);
			if (j>=0)
				value|=obs[j] << (max_val * (order-1));
		}
		obs[i]=value;
		//ASSERT(value<num_words) ;
	}
	if (start>0)
		for (i=start; i<sequence_length; i++)	
			obs[i-start]=obs[i];
}

void CDynProg::reset_svm_value(INT pos, INT & last_svm_pos, DREAL * svm_value) 
{
	for (int i=0; i<num_words_single; i++)
		word_used_single[i]=false ;
	for (INT s=0; s<num_svms; s++)
		svm_value_unnormalized_single[s] = 0 ;
	for (INT s=0; s<num_svms; s++)
		svm_value[s] = 0 ;
	last_svm_pos = pos - 6+1 ;
	num_unique_words_single=0 ;
}

void CDynProg::extend_svm_value(WORD* wordstr, INT pos, INT &last_svm_pos, DREAL* svm_value) 
{
	bool did_something = false ;
	for (int i=last_svm_pos-1; (i>=pos) && (i>=0); i--)
	{
		if (wordstr[i]>=num_words_single)
			CIO::message(M_DEBUG, "wordstr[%i]=%i\n", i, wordstr[i]) ;
		
		if (!word_used_single[wordstr[i]])
		{
			for (INT s=0; s<num_svms_single; s++)
				svm_value_unnormalized_single[s]+=dict_weights.element(wordstr[i],s) ;
			
			word_used_single[wordstr[i]]=true ;
			num_unique_words_single++ ;
			did_something=true ;
		}
	} ;
	if (num_unique_words_single>0)
	{
		last_svm_pos=pos ;
		if (did_something)
			for (INT s=0; s<num_svms; s++)
				svm_value[s]= svm_value_unnormalized_single[s]/sqrt((double)num_unique_words_single) ;  // full normalization
	}
	else
	{
		// what should I do?
		for (INT s=0; s<num_svms; s++)
			svm_value[s]=0 ;
	}
	
}


void CDynProg::reset_segment_sum_value(INT num_states, INT pos, INT & last_segment_sum_pos, DREAL * segment_sum_value) 
{
	for (INT s=0; s<num_states; s++)
		segment_sum_value[s] = 0 ;
	last_segment_sum_pos = pos ;
	//fprintf(stderr, "start: %i\n", pos) ;
}

void CDynProg::extend_segment_sum_value(DREAL *segment_sum_weights, INT seqlen, INT num_states,
							  INT pos, INT &last_segment_sum_pos, DREAL* segment_sum_value) 
{
	for (int i=last_segment_sum_pos-1; (i>=pos) && (i>=0); i--)
	{
		for (INT s=0; s<num_states; s++)
			segment_sum_value[s] += segment_sum_weights[i*num_states+s] ;
	} ;
	//fprintf(stderr, "extend %i: %f\n", pos, segment_sum_value[0]) ;
	last_segment_sum_pos = pos ;
}


//#define PSI(t,j,k) psi[nbest*((t)*N+(j))+(k)]	
//#define DELTA(t,j,k) delta[(j)*nbest*max_look_back+((t)%max_look_back)*nbest+k]
//#define ktable.element(t,j,k) ktable[nbest*((t)*N+j)+k]
//#define ptable.element(t,j,k) ptable[nbest*((t)*N+j)+k]
//#define delta_end.element(k) delta_end[k]
//#define ktable_end.element(k) ktable_end[k]
//#define path_ends.element(k) path_end[k]
//#define seq.element(j,t) seq[j+(t)*N]
//#define PEN(i,j) PEN_matrix[(j)*N+i]



void CDynProg::best_path_2struct(const DREAL *seq_array, INT seq_len, const INT *pos,
							 CPlif **PEN_matrix, 
							 const char *genestr, INT genestr_len,
							 short int nbest, 
							 DREAL *prob_nbest, INT *my_state_seq, INT *my_pos_seq,
							 DREAL *dictionary_weights, INT dict_len, DREAL *segment_sum_weights, 
							 DREAL *&PEN_values, DREAL *&PEN_input_values, INT &num_PEN_id)
{
	const INT default_look_back = 100 ;
	INT max_look_back = default_look_back ;
	bool use_svm = false ;
	ASSERT(dict_len==num_svms*num_words_single) ;
	dict_weights.set_array(dictionary_weights, dict_len, num_svms, false) ;

	CDynamicArray2<CPlif*> PEN(PEN_matrix, N, N, false) ;
	CDynamicArray2<DREAL> seq((DREAL *)seq_array, N, seq_len, false) ;
	
	DREAL svm_value[num_svms] ;
	DREAL segment_sum_value[N] ;
	
	{ // initialize svm_svalue
		for (INT s=0; s<num_svms; s++)
			svm_value[s]=0 ;
	}
	
	{ // determine maximal length of look-back
		for (INT i=0; i<N; i++)
			for (INT j=0; j<N; j++)
			{
				CPlif *penij=PEN.element(i,j) ;
				while (penij!=NULL)
				{
					if (penij->get_max_len()>max_look_back)
						max_look_back=penij->get_max_len() ;
					if (penij->get_use_svm())
						use_svm=true ;
					if (penij->get_id()+1>num_PEN_id)
						num_PEN_id=penij->get_id()+1 ;
					penij=penij->get_next_pen() ;
				} 
			}
	}
	max_look_back = CMath::min(genestr_len, max_look_back) ;
	//fprintf(stderr,"use_svm=%i\n", use_svm) ;
	fprintf(stderr,"max_look_back=%i\n", max_look_back) ;
	
	const INT look_back_buflen = (max_look_back+1)*nbest*N ;
	//fprintf(stderr,"look_back_buflen=%i\n", look_back_buflen) ;
	const DREAL mem_use = (DREAL)(seq_len*N*nbest*(sizeof(T_STATES)+sizeof(short int)+sizeof(INT))+
								look_back_buflen*(2*sizeof(DREAL)+sizeof(INT))+
								seq_len*(sizeof(T_STATES)+sizeof(INT))+
								genestr_len*sizeof(bool))/(1024*1024)
		 ;
    bool is_big = (mem_use>200) || (seq_len>5000) ;

	if (is_big)
	{
		CIO::message(M_DEBUG,"calling best_path_trans: seq_len=%i, N=%i, lookback=%i nbest=%i\n", 
					 seq_len, N, max_look_back, nbest) ;
		CIO::message(M_DEBUG,"allocating %1.2fMB of memory\n", 
					 mem_use) ;
	}
	ASSERT(nbest<32000) ;
		
	CDynamicArray3<DREAL> delta(max_look_back+1,nbest,N) ;
	CDynamicArray3<T_STATES> psi(seq_len,N,nbest) ;
	CDynamicArray3<short int> ktable(seq_len,N,nbest) ;
	CDynamicArray3<INT> ptable(seq_len,N,nbest) ;

	CDynamicArray<DREAL> delta_end(nbest) ;
	CDynamicArray<T_STATES> path_ends(nbest) ;
	CDynamicArray<short int> ktable_end(nbest) ;

	CDynamicArray<DREAL> tempvv(look_back_buflen) ;
	CDynamicArray<INT> tempii(look_back_buflen) ;

	CDynamicArray<T_STATES> state_seq(seq_len) ;
	CDynamicArray<INT> pos_seq(seq_len) ;

	// translate to words, if svm is used
	WORD* wordstr=NULL ;
	if (use_svm)
	{
		ASSERT(dictionary_weights!=NULL) ;
		wordstr=new WORD[genestr_len] ;
		for (INT i=0; i<genestr_len; i++)
			switch (genestr[i])
			{
			case 'a': wordstr[i]=0 ; break ;
			case 'c': wordstr[i]=1 ; break ;
			case 'g': wordstr[i]=2 ; break ;
			case 't': wordstr[i]=3 ; break ;
			default: ASSERT(0) ;
			}
		translate_from_single_order(wordstr, genestr_len, word_degree_single-1, word_degree_single) ;
	}
	
	
	{ // initialization
		for (T_STATES i=0; i<N; i++)
		{
			delta.element(0,i,0) = get_p(i) + seq.element(i,0) ;
			psi.element(0,i,0)   = 0 ;
			ktable.element(0,i,0)  = 0 ;
			ptable.element(0,i,0)  = 0 ;
			for (short int k=1; k<nbest; k++)
			{
				delta.element(0,i,k)    = -CMath::INFTY ;
				psi.element(0,i,0)      = 0 ;
				ktable.element(0,i,k)     = 0 ;
				ptable.element(0,i,k)     = 0 ;
			}
		}
	}

	// recursion
	for (INT t=1; t<seq_len; t++)
	{
		if (is_big && t%(seq_len/1000)==1)
			CIO::progress(t, 0, seq_len);
		
		for (T_STATES j=0; j<N; j++)
		{
			if (seq.element(j,t)<-1e20)
			{ // if we cannot observe the symbol here, then we can omit the rest
				for (short int k=0; k<nbest; k++)
				{
					delta.element(t,j,k)    = seq.element(j,t) ;
					psi.element(t,j,k)      = 0 ;
					ktable.element(t,j,k)     = 0 ;
					ptable.element(t,j,k)     = 0 ;
				}
			}
			else
			{
				const T_STATES num_elem   = trans_list_forward_cnt[j] ;
				const T_STATES *elem_list = trans_list_forward[j] ;
				const DREAL *elem_val      = trans_list_forward_val[j] ;
				
				INT list_len=0 ;
				for (INT i=0; i<num_elem; i++)
				{
					T_STATES ii = elem_list[i] ;
					//fprintf(stderr, "i=%i  ii=%i  num_elem=%i  PEN=%ld\n", i, ii, num_elem, PEN(j,ii)) ;
					
					const CPlif * penalty = PEN.element(j,ii) ;
					INT look_back = default_look_back ;
					if (penalty!=NULL)
						look_back=penalty->get_max_len() ;
					
					INT last_svm_pos ;
					if (use_svm)
						reset_svm_value(pos[t], last_svm_pos, svm_value) ;

					INT last_segment_sum_pos ;
					reset_segment_sum_value(N, pos[t], last_segment_sum_pos, segment_sum_value) ;

					for (INT ts=t-1; ts>=0 && pos[t]-pos[ts]<=look_back; ts--)
					{
						if (use_svm)
							extend_svm_value(wordstr, pos[ts], last_svm_pos, svm_value) ;

						extend_segment_sum_value(segment_sum_weights, seq_len, N, pos[ts], last_segment_sum_pos, segment_sum_value) ;
						
						DREAL input_value ;
						DREAL pen_val = penalty->lookup_penalty(pos[t]-pos[ts], svm_value, true, input_value) + segment_sum_value[j] ;
						for (short int diff=0; diff<nbest; diff++)
						{
							DREAL  val        = delta.element(ts,ii,diff) + elem_val[i] ;
							val             += pen_val ;
							
							tempvv[list_len] = -val ;
							tempii[list_len] =  ii + diff*N + ts*N*nbest;
							//fprintf(stderr, "%i (%i,%i,%i, %i, %i) ", list_len, diff, ts, i, pos[t]-pos[ts], look_back) ;
							list_len++ ;
						}
					}
				}
				CMath::nmin<INT>(tempvv.get_array(), tempii.get_array(), list_len, nbest) ;
				
				for (short int k=0; k<nbest; k++)
				{
					if (k<list_len)
					{
						delta.element(t,j,k)    = -tempvv[k] + seq.element(j,t);
						psi.element(t,j,k)      = (tempii[k]%N) ;
						ktable.element(t,j,k)     = (tempii[k]%(N*nbest)-psi.element(t,j,k))/N ;
						ptable.element(t,j,k)     = (tempii[k]-(tempii[k]%(N*nbest)))/(N*nbest) ;
					}
					else
					{
						delta.element(t,j,k)    = -CMath::INFTY ;
						psi.element(t,j,k)      = 0 ;
						ktable.element(t,j,k)     = 0 ;
						ptable.element(t,j,k)     = 0 ;
					}
				}
			}
		}
	}
	
	{ //termination
		INT list_len = 0 ;
		for (short int diff=0; diff<nbest; diff++)
		{
			for (T_STATES i=0; i<N; i++)
			{
				tempvv[list_len] = -(delta.element(seq_len-1,i,diff)+get_q(i)) ;
				tempii[list_len] = i + diff*N ;
				list_len++ ;
			}
		}
		
		CMath::nmin(tempvv.get_array(), tempii.get_array(), list_len, nbest) ;
		
		for (short int k=0; k<nbest; k++)
		{
			delta_end.element(k) = -tempvv[k] ;
			path_ends.element(k) = (tempii[k]%N) ;
			ktable_end.element(k) = (tempii[k]-path_ends.element(k))/N ;
		}
	}
	
	{ //state sequence backtracking		
		for (short int k=0; k<nbest; k++)
		{
			prob_nbest[k]= delta_end.element(k) ;
			
			INT i         = 0 ;
			state_seq[i]  = path_ends.element(k) ;
			short int q   = ktable_end.element(k) ;
			pos_seq[i]    = seq_len-1 ;

			while (pos_seq[i]>0)
			{
				//fprintf(stderr,"s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
				state_seq[i+1] = psi.element(pos_seq[i], state_seq[i], q);
				pos_seq[i+1]   = ptable.element(pos_seq[i], state_seq[i], q) ;
				q              = ktable.element(pos_seq[i], state_seq[i], q) ;
				i++ ;
			}
			//fprintf(stderr,"s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
			INT num_states = i+1 ;
			for (i=0; i<num_states;i++)
			{
				my_state_seq[i+k*(seq_len+1)] = state_seq[num_states-i-1] ;
				my_pos_seq[i+k*(seq_len+1)]   = pos_seq[num_states-i-1] ;
			}
			my_state_seq[num_states+k*(seq_len+1)]=-1 ;
			my_pos_seq[num_states+k*(seq_len+1)]=-1 ;
		}
		DREAL svm_value[num_svms] ;
		for (INT s=0; s<num_svms; s++)
			svm_value[s]=0 ;
		// one more for the emissions: the first
		num_PEN_id++ ;
		PEN_values = new DREAL[num_PEN_id*seq_len*nbest] ;
		for (INT s=0; s<num_PEN_id*seq_len*nbest; s++)
			PEN_values[s]=0 ;
		PEN_input_values = new DREAL[num_PEN_id*seq_len*nbest] ;
		for (INT s=0; s<num_PEN_id*seq_len*nbest; s++)
			PEN_input_values[s]=0 ;
		char * PEN_names[num_PEN_id] ;
		for (INT s=0; s<num_PEN_id; s++)
			PEN_names[s]=NULL ;

		for (short int k=0; k<nbest; k++)
		{
			for (INT i=0; i<seq_len-1; i++)
			{
				if (my_state_seq[i+1+k*(seq_len+1)]==-1)
					break ;
				INT from_state = my_state_seq[i+k*(seq_len+1)] ;
				INT to_state   = my_state_seq[i+1+k*(seq_len+1)] ;
				INT from_pos   = my_pos_seq[i+k*(seq_len+1)] ;
				INT to_pos     = my_pos_seq[i+1+k*(seq_len+1)] ;
				
				//CIO::message(M_DEBUG, "%i. from state %i pos %i[%i]  to  state %i pos %i[%i]  penalties:", k, from_state, pos[from_pos], from_pos, to_state, pos[to_pos], to_pos) ;
				INT last_svm_pos = -1 ;
				INT last_segment_sum_pos=-1 ;
								
				if (use_svm)
				{
					reset_svm_value(pos[to_pos], last_svm_pos, svm_value) ;
					extend_svm_value(wordstr, pos[from_pos], last_svm_pos, svm_value) ;
				}
				reset_segment_sum_value(N, pos[to_pos], last_segment_sum_pos, segment_sum_value) ;
				extend_segment_sum_value(segment_sum_weights, seq_len, N, pos[from_pos], last_segment_sum_pos, segment_sum_value) ;
				//fprintf(stderr, "%i -> %i  %f %f\n", pos[from_pos], pos[to_pos], segment_sum_value[0], segment_sum_value[1]) ;
				
				PEN_values[num_PEN_id-1 + i*num_PEN_id + seq_len*num_PEN_id*k] = seq.element(to_state, to_pos) + segment_sum_value[to_state] ;
				//PEN_input_values[num_PEN_id-1 + i*num_PEN_id + seq_len*num_PEN_id*k] = segment_sum_value[to_state] ;

				CPlif *penalty = PEN.element(to_state, from_state) ;
				while (penalty)
				{
					DREAL input_value=0 ;
					DREAL pen_val = penalty->lookup_penalty(pos[to_pos]-pos[from_pos], svm_value, false, input_value) ;
					PEN_values[penalty->get_id() + i*num_PEN_id + seq_len*num_PEN_id*k] += pen_val ;
					PEN_input_values[penalty->get_id() + i*num_PEN_id + seq_len*num_PEN_id*k] += input_value ;
					PEN_names[penalty->get_id()] = penalty->get_name() ;
					//CIO::message(M_DEBUG, "%s(%i;%1.2f), ", penalty->name, penalty->id, pen_val) ;
					penalty = penalty->get_next_pen() ;
				}
				//CIO::message(M_DEBUG, "\n") ;
			}
			/*for (INT s=0; s<num_PEN_id; s++)
			{
				if (PEN_names[s])
					CIO::message(M_DEBUG, "%s:\t%1.2f\n", PEN_names[s], PEN_values[s+num_PEN_id*k]) ;
				else
					ASSERT(PEN_values[s]==0.0) ;
					}*/
		}
	}
	if (is_big)
		CIO::message(M_MESSAGEONLY, "DONE.     \n") ;
}

void CDynProg::reset_svm_values(INT pos, INT * last_svm_pos, DREAL * svm_value) 
{
	for (INT j=0; j<num_degrees; j++)
	{
		for (INT i=0; i<num_words[j]; i++)
			word_used.element(j,i)=false ;
		for (INT s=0; s<num_svms; s++)
			svm_values_unnormalized.element(j,s) = 0 ;
		num_unique_words[j]=0 ;
		last_svm_pos[j] = pos - word_degree[j]+1 ;
		svm_pos_start[j] = pos - word_degree[j] ;
	}
	for (INT s=0; s<num_svms; s++)
		svm_value[s] = 0 ;
}

void CDynProg::extend_svm_values(WORD** wordstr, INT pos, INT *last_svm_pos, DREAL* svm_value) 
{
	bool did_something = false ;
	for (INT j=0; j<num_degrees; j++)
	{
		for (int i=last_svm_pos[j]-1; (i>=pos) && (i>=0); i--)
		{
			if (wordstr[j][i]>=num_words[j])
				CIO::message(M_DEBUG, "wordstr[%i]=%i\n", i, wordstr[j][i]) ;

			ASSERT(wordstr[j][i]<num_words[j]) ;
			if (!word_used.element(j,wordstr[j][i]))
			{
				for (INT s=0; s<num_svms; s++)
					svm_values_unnormalized.element(j,s)+=dict_weights.element(wordstr[j][i]+cum_num_words[j],s) ;
				//svm_values_unnormalized.element(j,s)+=dict_weights[wordstr[j][i]+s*cum_num_words[num_degrees]+cum_num_words[j]] ;
				
				word_used.element(j,wordstr[j][i])=true ;
				num_unique_words[j]++ ;
				did_something=true ;
			} ;
		} ;
		if (num_unique_words[j]>0)
			last_svm_pos[j]=pos ;
	} ;
	
	if (did_something)
		for (INT s=0; s<num_svms; s++)
		{
			svm_value[s]=0.0 ;
			for (INT j=0; j<num_degrees; j++)
				if (num_unique_words[j]>0)
					svm_value[s]+= svm_values_unnormalized.element(j,s)/sqrt((double)num_unique_words[j]) ;  // full normalization
		}
}


void CDynProg::init_svm_values(struct svm_values_struct & svs, INT start_pos, INT seqlen, INT howmuchlookback)
{
	/*
	  See find_svm_values_till_pos for comments
	  
	  svs.svm_values[i+s*svs.seqlen] has the value of the s-th SVM on genestr(pos(t_end-i):pos(t_end)) 
	  for every i satisfying pos(t_end)-pos(t_end-i) <= svs.maxlookback
	  
	  where t_end is the end of all segments we are currently looking at
	*/
	
	if (!svs.svm_values)
	{
		svs.num_unique_words        = new INT[num_degrees] ;
		svs.svm_values              = new DREAL[seqlen*num_svms] ;
		svs.svm_values_unnormalized = new DREAL*[num_degrees] ;
		svs.word_used               = new bool*[num_degrees] ;
		for (INT j=0; j<num_degrees; j++)
		{
			//svs.svm_values[j]              = new DREAL[seqlen*num_svms] ;
			svs.svm_values_unnormalized[j] = new DREAL[num_svms] ;
			svs.word_used[j]               = new bool[num_words[j]] ;
		}
	}
	
	for (INT i=0; i<seqlen*num_svms; i++)       // initializing this for safety, though we should be able to live without it
		svs.svm_values[i] = 0;

	for (INT j=0; j<num_degrees; j++)
	{		
		for (INT s=0; s<num_svms; s++)
			svs.svm_values_unnormalized[j][s] = 0 ;
		
		for (INT i=0; i<num_words[j]; i++)
			svs.word_used[j][i] = false ;

		svs.num_unique_words[j] = 0 ;
	}
	
	svs.maxlookback = howmuchlookback ;
	svs.seqlen = seqlen;
}

void CDynProg::clear_svm_values(struct svm_values_struct & svs) 
{
	if (NULL != svs.svm_values)
	{
		for (INT j=0; j<num_degrees; j++)
			delete[] svs.word_used[j] ;
		for (INT j=0; j<num_degrees; j++)
			delete[] svs.svm_values_unnormalized[j] ;
		
		delete[] svs.svm_values_unnormalized;
		delete[] svs.svm_values;
		delete[] svs.word_used;
		svs.word_used=NULL ;
		svs.svm_values=NULL ;
		svs.svm_values_unnormalized=NULL ;
	}
}


void CDynProg::find_svm_values_till_pos(WORD** wordstr,  const INT *pos,  INT t_end, struct svm_values_struct &svs)
{
	/*
	  wordstr is a vector of L n-gram indices, with wordstr(i) representing a number betweeen 0 and 4095 corresponding to the 6-mer in genestr(i-5:i) 
	  pos is a vector of candidate transition positions (it is input to best_path_trans)
	  t_end is some index in pos
	  
	  svs has been initialized by init_svm_values
	  
	  At the end of this procedure, 
	  svs.svm_values[i+s*svs.seqlen] has the value of the s-th SVM on genestr(pos(t_end-i):pos(t_end)) 
	  for every i satisfying pos(t_end)-pos(t_end-i) <= svs.maxlookback
	  
	  The SVM weights are precomputed in dict_weights
	*/
	for (INT j=0; j<num_degrees; j++)
	{
		INT plen = 1;
		INT ts = t_end-1;        // index in pos; pos(ts) and pos(t) are indices of wordstr
		INT offset;
		
		/*
		  for (INT s=0; s<num_svms; s++)
		  {
		  offset = s*svs.seqlen;
		  for (INT i=0;i<word_degree; i++)
		  svs.svm_values[i+offset] = 0;
		  }
		*/
		
		INT posprev = pos[t_end]-word_degree[j]+1;
		INT poscurrent = pos[ts];
		
		if (poscurrent<0)
			poscurrent = 0;
		
		INT len = pos[t_end] - poscurrent;
		
		while ((ts>=0) && (len <= svs.maxlookback))
		{
			for (int i=posprev-1 ; (i>=poscurrent) && (i>=0) ; i--)
			{
				// 	  if (word_degree > (pos[t_end]-pos[ts]))
				//	    fprintf(fid, " *******  i=%d , wordstr[i]=%d   dict_weights[1,wordstr[i]]=%f  t_end=%d, ts=%d  pos[t_end]=%d  pos[ts]=%d   posprev=%d\n", i, wordstr[i], dict_weights[wordstr[i]], t_end,ts,pos[t_end],pos[ts],posprev);
				
				if (wordstr[j][i]>=num_words[j])
					fprintf(stderr, "wordstr[%i][%i]=%i\n", j, i, wordstr[j][i]) ;
				ASSERT(wordstr[j][i]<num_words[j]) ;
				if (!svs.word_used[j][wordstr[j][i]])
				{
					for (INT s=0; s<num_svms; s++)
						svs.svm_values_unnormalized[j][s]+=dict_weights.element(wordstr[j][i]+cum_num_words[j], s) ;
					
					svs.word_used[j][wordstr[j][i]]=true ;
					svs.num_unique_words[j]++ ;
				}
			}
			double normalization_factor = 1.0;
			if (svs.num_unique_words[j] > 0)
				normalization_factor = sqrt((double)svs.num_unique_words[j]);
			for (INT s=0; s<num_svms; s++)
			{
				offset = s*svs.seqlen;
				if (j==0)
					svs.svm_values[offset+plen]=0 ;
				svs.svm_values[offset+plen] += svs.svm_values_unnormalized[j][s] / normalization_factor;
			}
			
			if (posprev > poscurrent)         // remember posprev initially set to pos[t_end]-word_degree+1... pos[ts] could be e.g. pos[t_end]-2
				posprev = poscurrent;           
			
			ts--;
			plen++;
			
			if (ts>=0)
			{
				poscurrent=pos[ts];
				if (poscurrent<0)
					poscurrent = 0;
				len = pos[t_end] - poscurrent;
			}
		}
	}
}


bool CDynProg::extend_orf(const CDynamicArray<bool>& genestr_stop, INT orf_from, INT orf_to, INT start, INT &last_pos, INT to)
{
	if (start<0) 
		start=0 ;
	if (to<0)
		to=0 ;
	
	INT orf_target = orf_to-orf_from ;
	if (orf_target<0) orf_target+=3 ;
	
	INT pos ;
	if (last_pos==to)
		pos = to-orf_to-3 ;
	else
		pos=last_pos ;

	if (pos<0)
		return true ;
	
	for (; pos>=start; pos-=3)
		if (genestr_stop[pos])
			return false ;
	
	last_pos = CMath::min(pos+3,to-orf_to-3) ;

	return true ;
}


//#define PSI(t,j,k) psi[nbest*((t)*N+(j))+(k)]	
//#define delta->elements(t,j,k) delta[(j)*nbest*max_look_back+((t)%max_look_back)*nbest+k]
//#define ktable.element(t,j,k) ktable[nbest*((t)*N+j)+k]
//#define ptable.element(t,j,k) ptable[nbest*((t)*N+j)+k]
//#define delta_end.element(k) delta_end[k]
//#define ktable_end.element(k) ktable_end[k]
//#define path_ends.element(k) path_end[k]
//#define seq.element(j,t) seq[j+(t)*N]
//#define PEN(i,j) PEN_matrix[(j)*N+i]
#define ORF_FROM(i) orf_info[i] 
#define ORF_TO(i) orf_info[N+i] 

void CDynProg::best_path_trans(const DREAL *seq_array, INT seq_len, const INT *pos, const INT *orf_info,
							   CPlif **PEN_matrix, 
							   const char *genestr, INT genestr_len,
							   short int nbest, 
							   DREAL *prob_nbest, INT *my_state_seq, INT *my_pos_seq,
							   DREAL *dictionary_weights, INT dict_len, 
							   DREAL *&PEN_values, DREAL *&PEN_input_values, 
							   INT &num_PEN_id, bool use_orf)
{
	//fprintf(stderr, "use_orf=%i\n", use_orf) ;
	
	const INT default_look_back = 30000 ;
	INT max_look_back = default_look_back ;
	bool use_svm = false ;
	ASSERT(dict_len==num_svms*cum_num_words[num_degrees]) ;
	dict_weights.set_array(dictionary_weights, cum_num_words[num_degrees], num_svms, false) ;
	int offset=0;
	
	DREAL svm_value[num_svms] ;
	CDynamicArray2<CPlif*> PEN(PEN_matrix, N, N, false) ;
	CDynamicArray2<DREAL> seq((DREAL *)seq_array, N, seq_len, false) ;
	
	{ // initialize svm_svalue
		for (INT s=0; s<num_svms; s++)
			svm_value[s]=0 ;
	}
	
	{ // determine maximal length of look-back
		for (INT i=0; i<N; i++)
			for (INT j=0; j<N; j++)
			{
				CPlif *penij=PEN.element(i,j) ;
				while (penij!=NULL)
				{
					if (penij->get_max_len()>max_look_back)
						max_look_back=penij->get_max_len() ;
					if (penij->get_use_svm())
						use_svm=true ;
					if (penij->get_id()+1>num_PEN_id)
						num_PEN_id=penij->get_id()+1 ;
					penij=penij->get_next_pen() ;
				} 
			}
	}
	max_look_back = CMath::min(genestr_len, max_look_back) ;
	//fprintf(stderr,"use_svm=%i\n", use_svm) ;
	
	
	const INT look_back_buflen = max_look_back*nbest*N ;
	const DREAL mem_use = (DREAL)(seq_len*N*nbest*(sizeof(T_STATES)+sizeof(short int)+sizeof(INT))+
								look_back_buflen*(2*sizeof(DREAL)+sizeof(INT))+
								seq_len*(sizeof(T_STATES)+sizeof(INT))+
								genestr_len*sizeof(bool))/(1024*1024)
		 ;
    bool is_big = (mem_use>200) || (seq_len>5000) ;

	if (is_big)
	{
		CIO::message(M_DEBUG,"calling best_path_trans: seq_len=%i, N=%i, lookback=%i nbest=%i\n", 
					 seq_len, N, max_look_back, nbest) ;
		CIO::message(M_DEBUG,"allocating %1.2fMB of memory\n", 
					 mem_use) ;
	}
	ASSERT(nbest<32000) ;
		
	CDynamicArray<bool> genestr_stop(genestr_len) ;

	CDynamicArray3<DREAL> delta(max_look_back, nbest, N) ;
	CDynamicArray3<T_STATES> psi(seq_len,N,nbest) ;
	CDynamicArray3<short int> ktable(seq_len,N,nbest) ;
	CDynamicArray3<INT> ptable(seq_len,N,nbest) ;

	CDynamicArray<DREAL> delta_end(nbest) ;
	CDynamicArray<T_STATES> path_ends(nbest) ;
	CDynamicArray<short int> ktable_end(nbest) ;

#if USEFIXEDLENLIST > 0
	CDynamicArray<DREAL> fixedtempvv(look_back_buflen) ;
	CDynamicArray<INT> fixedtempii(look_back_buflen) ;
#endif


	// we always use oldtempvv and oldtempii, even if USEORIGINALLIST is 0
	// as i didnt change the backtracking stuff

	CDynamicArray<DREAL> oldtempvv(look_back_buflen) ;
	CDynamicArray<INT> oldtempii(look_back_buflen) ;


	CDynamicArray<T_STATES> state_seq(seq_len) ;
	CDynamicArray<INT> pos_seq(seq_len) ;

	{ // precompute stop codons
		for (INT i=0; i<genestr_len-2; i++)
			if (genestr[i]=='t' && 
				((genestr[i+1]=='a' && 
				  (genestr[i+2]=='a' || genestr[i+2]=='g')) ||
				 (genestr[i+1]=='g' && genestr[i+2]=='a')))
				genestr_stop[i]=true ;
			else
				genestr_stop[i]=false ;
		genestr_stop[genestr_len-1]=false ;
		genestr_stop[genestr_len-1]=false ;
	}

	// translate to words, if svm is used
	WORD* wordstr[num_degrees] ;
	{
		for (INT j=0; j<num_degrees; j++)
		{
			wordstr[j]=NULL ;
			if (use_svm)
			{
				ASSERT(dictionary_weights!=NULL) ;
				wordstr[j]=new WORD[genestr_len] ;
				for (INT i=0; i<genestr_len; i++)
					switch (genestr[i])
					{
					case 'a': wordstr[j][i]=0 ; break ;
					case 'c': wordstr[j][i]=1 ; break ;
					case 'g': wordstr[j][i]=2 ; break ;
					case 't': wordstr[j][i]=3 ; break ;
					default: ASSERT(0) ;
					}
				translate_from_single_order(wordstr[j], genestr_len,
											word_degree[j]-1, word_degree[j]) ;
			}
		}
	}
	
  	
	{ // initialization

		for (T_STATES i=0; i<N; i++)
		{
			delta.element(0,i,0) = get_p(i) + seq.element(i,0) ;        // get_p defined in HMM.h to be equiv to initial_state_distribution
			psi.element(0,i,0)   = 0 ;
			ktable.element(0,i,0)  = 0 ;
			ptable.element(0,i,0)  = 0 ;
			for (short int k=1; k<nbest; k++)
			{
				delta.element(0,i,k)    = -CMath::INFTY ;
				psi.element(0,i,0)      = 0 ;                  // <--- what's this for?
				ktable.element(0,i,k)     = 0 ;
				ptable.element(0,i,k)     = 0 ;
			}
		}
	}

	struct svm_values_struct svs;
	svs.num_unique_words = NULL;
	svs.svm_values = NULL;
	svs.svm_values_unnormalized = NULL;
	svs.word_used = NULL;

	// recursion
	for (INT t=1; t<seq_len; t++)
	{
		if (is_big && t%(seq_len/1000)==1)
			CIO::progress(t, 0, seq_len);
		
		init_svm_values(svs, t, seq_len, max_look_back);
		find_svm_values_till_pos(wordstr, pos, t, svs);  
	
		for (T_STATES j=0; j<N; j++)
		{
			if (seq.element(j,t)<-1e20)
			{ // if we cannot observe the symbol here, then we can omit the rest
				for (short int k=0; k<nbest; k++)
				{
					delta.element(t,j,k)    = seq.element(j,t) ;
					psi.element(t,j,k)      = 0 ;
					ktable.element(t,j,k)     = 0 ;
					ptable.element(t,j,k)     = 0 ;
				}
			}
			else
			{
				const T_STATES num_elem   = trans_list_forward_cnt[j] ;
				const T_STATES *elem_list = trans_list_forward[j] ;
				const DREAL *elem_val      = trans_list_forward_val[j] ;
				
#if USEFIXEDLENLIST > 0
				INT fixed_list_len = 0 ;
#endif
				
#if USEORIGINALLIST > 0
				INT old_list_len = 0 ;
#endif
				
#if USEHEAP > 0
				Heap* tempheap = new Heap;
#endif
				
				for (INT i=0; i<num_elem; i++)
				{
					T_STATES ii = elem_list[i] ;
					
					const CPlif * penalty = PEN.element(j,ii) ;
					INT look_back = default_look_back ;
					if (penalty!=NULL)
						look_back=penalty->get_max_len() ;
					INT orf_from = ORF_FROM(ii) ;
					INT orf_to   = ORF_TO(j) ;
					if((orf_from!=-1)!=(orf_to!=-1))
						fprintf(stderr,"j=%i  ii=%i  orf_from=%i orf_to=%i p=%1.2f\n", j, ii, orf_from, orf_to, elem_val[i]) ;
					ASSERT((orf_from!=-1)==(orf_to!=-1)) ;
					
					INT orf_target = -1 ;
					if (orf_from!=-1)
					{
						orf_target=orf_to-orf_from ;
						if (orf_target<0) orf_target+=3 ;
						ASSERT(orf_target>=0 && orf_target<3) ;
					}
					
					INT last_pos = pos[t] ;
					
					for (INT ts=t-1; ts>=0 && pos[t]-pos[ts]<=look_back; ts--)
					{
						bool ok ;
						int plen=t-ts;
						
						if (orf_target==-1)
							ok=true ;
						else if (pos[ts]!=-1 && (pos[t]-pos[ts])%3==orf_target)
						{
							ok=(!use_orf) || extend_orf(genestr_stop, orf_from, orf_to, pos[ts], last_pos, pos[t]) ;
							if (!ok) 
							{
								//CIO::message(M_DEBUG, "no orf from %i[%i] to %i[%i]\n", pos[ts], orf_from, pos[t], orf_to) ;
								break ;
							}
						} else
							ok=false ;
						
						if (ok)
						{

						  for (INT ss=0; ss<num_svms; ss++)
						    {
						      offset = ss*svs.seqlen;
						      svm_value[ss]=svs.svm_values[offset+plen];
						    }

						  DREAL input_value ;
						  DREAL pen_val = penalty->lookup_penalty(pos[t]-pos[ts], svm_value, true, input_value) ;
						  for (short int diff=0; diff<nbest; diff++)
						    {
						      DREAL  val        = delta.element(ts,ii,diff) + elem_val[i] ;
						      val             += pen_val ;
						      DREAL mval = -val;

                                                      #if USEHEAP > 0
						      tempheap->Insert(mval,ii + diff*N + ts*N*nbest);
						      #endif

						      #if USEORIGINALLIST > 0
						      oldtempvv[old_list_len] = mval ;
						      oldtempii[old_list_len] = ii + diff*N + ts*N*nbest;
						      old_list_len++ ;
						      #endif

						      #if USEFIXEDLENLIST > 0
						      
						      /* only place -val in fixedtempvv if it is one of the nbest lowest values in there */
						      /* fixedtempvv[i], i=0:nbest-1, is sorted so that fixedtempvv[0] <= fixedtempvv[1] <= ...*/
						      /* fixed_list_len has the number of elements in fixedtempvv */

						      if ((fixed_list_len < nbest) || (mval < fixedtempvv[fixed_list_len-1]))
							{
							  if ( (fixed_list_len<nbest) && ((0==fixed_list_len) || (mval>fixedtempvv[fixed_list_len-1])) )
							    {
							      fixedtempvv[fixed_list_len] = mval ;
							      fixedtempii[fixed_list_len] = ii + diff*N + ts*N*nbest;
							      fixed_list_len++ ;
							    }
							  else  // must have mval < fixedtempvv[fixed_list_len-1]
							    {
							      int addhere = fixed_list_len;
							      while ((addhere > 0) && (mval < fixedtempvv[addhere-1]))
								addhere--;
							      
							      // move everything from addhere+1 one forward 
							      
							      for (int jj=fixed_list_len-1; jj>addhere; jj--)
								{
								  fixedtempvv[jj] = fixedtempvv[jj-1];
								  fixedtempii[jj] = fixedtempii[jj-1];
								}
							      
							      fixedtempvv[addhere] = mval;
							      fixedtempii[addhere] = ii + diff*N + ts*N*nbest;
							      
							      if (fixed_list_len < nbest)
								fixed_list_len++;
							    }
							}
						      #endif

						    }
						}
					}
				}
				
				#if USEORIGINALLIST > 0
				CMath::nmin<INT>(oldtempvv, oldtempii, old_list_len, nbest) ;
				#endif


				int numEnt = 0;
				#if USEHEAP == 2
				numEnt = tempheap->GetNumNodes();
				#elif USEORIGINALLIST == 2
				numEnt = old_list_len;
				#elif USEFIXEDLENLIST == 2
				numEnt = fixed_list_len;
                                #endif

				double minusscore;
				long int fromtjk;

				for (short int k=0; k<nbest; k++)
				{
					if (k<numEnt)
					{
#if (USEHEAP == 2)
					    tempheap->ExtractMin(minusscore,fromtjk);
#elif (USEORIGINALLIST == 2)
					    minusscore = oldtempvv[k];
					    fromtjk = oldtempii[k];
#elif (USEFIXEDLENLIST == 2)
					    minusscore = fixedtempvv[k];
					    fromtjk = fixedtempii[k];
#endif
					    delta.element(t,j,k)    = -minusscore + seq.element(j,t);
					    psi.element(t,j,k)      = (fromtjk%N) ;
					    ktable.element(t,j,k)     = (fromtjk%(N*nbest)-psi.element(t,j,k))/N ;
					    ptable.element(t,j,k)     = (fromtjk-(fromtjk%(N*nbest)))/(N*nbest) ;
					}
					else
					{
						delta.element(t,j,k)    = -CMath::INFTY ;
						psi.element(t,j,k)      = 0 ;
						ktable.element(t,j,k)     = 0 ;
						ptable.element(t,j,k)     = 0 ;
					}
				}

				#if USEHEAP > 0
				delete tempheap;
				#endif
			}
		}
	}

	clear_svm_values(svs);


	
	{ //termination
		INT list_len = 0 ;
		for (short int diff=0; diff<nbest; diff++)
		{
			for (T_STATES i=0; i<N; i++)
			{
				oldtempvv[list_len] = -(delta.element(seq_len-1,i,diff)+get_q(i)) ;
				oldtempii[list_len] = i + diff*N ;
				list_len++ ;
			}
		}
		
		CMath::nmin(oldtempvv.get_array(), oldtempii.get_array(), list_len, nbest) ;
		
		for (short int k=0; k<nbest; k++)
		{
			delta_end.element(k) = -oldtempvv[k] ;
			path_ends.element(k) = (oldtempii[k]%N) ;
			ktable_end.element(k) = (oldtempii[k]-path_ends.element(k))/N ;
		}
	}
	
	{ //state sequence backtracking		
		for (short int k=0; k<nbest; k++)
		{
			prob_nbest[k]= delta_end.element(k) ;
			
			INT i         = 0 ;
			state_seq[i]  = path_ends.element(k) ;
			short int q   = ktable_end.element(k) ;
			pos_seq[i]    = seq_len-1 ;

			while (pos_seq[i]>0)
			{
				//fprintf(stderr,"s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
				state_seq[i+1] = psi.element(pos_seq[i], state_seq[i], q);
				pos_seq[i+1]   = ptable.element(pos_seq[i], state_seq[i], q) ;
				q              = ktable.element(pos_seq[i], state_seq[i], q) ;
				i++ ;
			}
			//fprintf(stderr,"s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
			INT num_states = i+1 ;
			for (i=0; i<num_states;i++)
			{
				my_state_seq[i+k*seq_len] = state_seq[num_states-i-1] ;
				my_pos_seq[i+k*seq_len]   = pos_seq[num_states-i-1] ;
			}
			my_state_seq[num_states+k*seq_len]=-1 ;
			my_pos_seq[num_states+k*seq_len]=-1 ;
		}

		DREAL svm_value[num_svms] ;
		for (INT s=0; s<num_svms; s++)
			svm_value[s]=0 ;

		// one more for the emissions: the first
		num_PEN_id++ ;
        // allocate memory
		PEN_values = new DREAL[num_PEN_id*seq_len*nbest] ;
		for (INT s=0; s<num_PEN_id*seq_len*nbest; s++)
			PEN_values[s]=0 ;
		PEN_input_values = new DREAL[num_PEN_id*seq_len*nbest] ;
		for (INT s=0; s<num_PEN_id*seq_len*nbest; s++)
			PEN_input_values[s]=0 ;
		char * PEN_names[num_PEN_id] ;
		for (INT s=0; s<num_PEN_id; s++)
			PEN_names[s]=NULL ;

		for (short int k=0; k<nbest; k++)
		{
			for (INT i=0; i<seq_len-1; i++)
			{
				if (my_state_seq[i+1+k*seq_len]==-1)
					break ;
				INT from_state = my_state_seq[i+k*seq_len] ;
				INT to_state   = my_state_seq[i+1+k*seq_len] ;
				INT from_pos   = my_pos_seq[i+k*seq_len] ;
				INT to_pos     = my_pos_seq[i+1+k*seq_len] ;
				
				//CIO::message(M_DEBUG, "%i. from state %i pos %i[%i]  to  state %i pos %i[%i]  penalties:", k, from_state, pos[from_pos], from_pos, to_state, pos[to_pos], to_pos) ;
				
				INT last_svm_pos[num_degrees] ;
				for (INT qq=0; qq<num_degrees; qq++)
					last_svm_pos[qq]=-1 ;
				
				reset_svm_values(pos[to_pos], last_svm_pos, svm_value) ;
				extend_svm_values(wordstr, pos[from_pos], last_svm_pos, svm_value) ;

				PEN_values[num_PEN_id-1 + i*num_PEN_id + seq_len*num_PEN_id*k] += seq.element(to_state, to_pos) ;
				PEN_input_values[num_PEN_id-1 + i*num_PEN_id + seq_len*num_PEN_id*k] = to_state + to_pos*1000 ;

				CPlif *penalty = PEN.element(to_state, from_state) ;
				while (penalty)
				{
					DREAL input_value=0 ;
					DREAL pen_val = penalty->lookup_penalty(pos[to_pos]-pos[from_pos], svm_value, false, input_value) ;
					PEN_values[penalty->get_id() + i*num_PEN_id + seq_len*num_PEN_id*k] += pen_val ;
					PEN_input_values[penalty->get_id() + i*num_PEN_id + seq_len*num_PEN_id*k] += input_value ;
					PEN_names[penalty->get_id()] = penalty->get_name() ;
					//CIO::message(M_DEBUG, "%s(%i;%1.2f), ", penalty->name, penalty->id, pen_val) ;
					penalty = penalty->get_next_pen() ;
				}
				//CIO::message(M_DEBUG, "\n") ;
			}
			/*for (INT s=0; s<num_PEN_id; s++)
			{
				if (PEN_names[s])
					CIO::message(M_DEBUG, "%s:\t%1.2f\n", PEN_names[s], PEN_values[s+num_PEN_id*k]) ;
				else
					ASSERT(PEN_values[s]==0.0) ;
					}*/
		}
	}
	if (is_big)
		CIO::message(M_MESSAGEONLY, "DONE.     \n") ;

	for (INT j=0; j<num_degrees; j++)
		delete[] wordstr[j] ;

}


/*#define PSI(t,j,k) psi[nbest*((t)*N+(j))+(k)]	
#define DELTA(t,j,k) delta[(j)*nbest*max_look_back+((t)%max_look_back)*nbest+k]
#define ktable.element(t,j,k) ktable[nbest*((t)*N+j)+k]
#define ptable.element(t,j,k) ptable[nbest*((t)*N+j)+k]
#define delta_end.element(k) delta_end[k]
#define ktable_end.element(k) ktable_end[k]
#define path_ends.element(k) path_end[k]
#define seq.element(j,t) seq[j+(t)*N]*/

void CDynProg::best_path_trans_simple(const DREAL *seq_array, INT seq_len, short int nbest, 
									  DREAL *prob_nbest, INT *my_state_seq)
{
	INT max_look_back = 2 ;
	const INT look_back_buflen = max_look_back*nbest*N ;
	ASSERT(nbest<32000) ;
		
	CDynamicArray2<DREAL> seq((DREAL *)seq_array, N, seq_len, false) ;

	CDynamicArray3<DREAL> delta(max_look_back, nbest, N) ;
	CDynamicArray3<T_STATES> psi(seq_len, N, nbest) ;
	CDynamicArray3<short int> ktable(seq_len,N,nbest) ;
	CDynamicArray3<INT> ptable(seq_len,N,nbest) ;

	CDynamicArray<DREAL> delta_end(nbest) ;
	CDynamicArray<T_STATES> path_ends(nbest) ;
	CDynamicArray<short int> ktable_end(nbest) ;

	CDynamicArray<DREAL> oldtempvv(look_back_buflen) ;
	CDynamicArray<INT> oldtempii(look_back_buflen) ;

	CDynamicArray<T_STATES> state_seq(seq_len) ;
	CDynamicArray<INT> pos_seq(seq_len) ;

	{ // initialization

		for (T_STATES i=0; i<N; i++)
		{
			delta.element(0,i,0) = get_p(i) + seq.element(i,0) ;        // get_p defined in HMM.h to be equiv to initial_state_distribution
			psi.element(0,i,0)   = 0 ;
			ktable.element(0,i,0)  = 0 ;
			ptable.element(0,i,0)  = 0 ;
			for (short int k=1; k<nbest; k++)
			{
				delta.element(0,i,k)    = -CMath::INFTY ;
				psi.element(0,i,0)      = 0 ;                  // <--- what's this for?
				ktable.element(0,i,k)     = 0 ;
				ptable.element(0,i,k)     = 0 ;
			}
		}
	}

	// recursion
	for (INT t=1; t<seq_len; t++)
	{
		for (T_STATES j=0; j<N; j++)
		{
			if (seq.element(j,t)<-1e20)
			{ // if we cannot observe the symbol here, then we can omit the rest
				for (short int k=0; k<nbest; k++)
				{
					delta.element(t,j,k)    = seq.element(j,t) ;
					psi.element(t,j,k)      = 0 ;
					ktable.element(t,j,k)     = 0 ;
					ptable.element(t,j,k)     = 0 ;
				}
			}
			else
			{
				const T_STATES num_elem   = trans_list_forward_cnt[j] ;
				const T_STATES *elem_list = trans_list_forward[j] ;
				const DREAL *elem_val      = trans_list_forward_val[j] ;
				
				INT old_list_len = 0 ;
				
				for (INT i=0; i<num_elem; i++)
				{
					T_STATES ii = elem_list[i] ;

					INT ts=t-1; 
					if (ts>=0)
					{
						bool ok=true ;
						
						if (ok)
						{

						  
						  for (short int diff=0; diff<nbest; diff++)
						    {
						      DREAL  val        = delta.element(ts,ii,diff) + elem_val[i] ;
						      DREAL mval = -val;

						      oldtempvv[old_list_len] = mval ;
						      oldtempii[old_list_len] = ii + diff*N + ts*N*nbest;
						      old_list_len++ ;
						    }
						}
					}
				}
				
				CMath::nmin<INT>(oldtempvv.get_array(), oldtempii.get_array(), old_list_len, nbest) ;

				int numEnt = 0;
				numEnt = old_list_len;

				double minusscore;
				long int fromtjk;

				for (short int k=0; k<nbest; k++)
				{
					if (k<numEnt)
					{
					    minusscore = oldtempvv[k];
					    fromtjk = oldtempii[k];
					    
					    delta.element(t,j,k)    = -minusscore + seq.element(j,t);
					    psi.element(t,j,k)      = (fromtjk%N) ;
					    ktable.element(t,j,k)     = (fromtjk%(N*nbest)-psi.element(t,j,k))/N ;
					    ptable.element(t,j,k)     = (fromtjk-(fromtjk%(N*nbest)))/(N*nbest) ;
					}
					else
					{
						delta.element(t,j,k)    = -CMath::INFTY ;
						psi.element(t,j,k)      = 0 ;
						ktable.element(t,j,k)     = 0 ;
						ptable.element(t,j,k)     = 0 ;
					}
				}
				
			}
		}
	}

	
	{ //termination
		INT list_len = 0 ;
		for (short int diff=0; diff<nbest; diff++)
		{
			for (T_STATES i=0; i<N; i++)
			{
				oldtempvv[list_len] = -(delta.element(seq_len-1,i,diff)+get_q(i)) ;
				oldtempii[list_len] = i + diff*N ;
				list_len++ ;
			}
		}
		
		CMath::nmin(oldtempvv.get_array(), oldtempii.get_array(), list_len, nbest) ;
		
		for (short int k=0; k<nbest; k++)
		{
			delta_end.element(k) = -oldtempvv[k] ;
			path_ends.element(k) = (oldtempii[k]%N) ;
			ktable_end.element(k) = (oldtempii[k]-path_ends.element(k))/N ;
		}
	}
	
	{ //state sequence backtracking		
		for (short int k=0; k<nbest; k++)
		{
			prob_nbest[k]= delta_end.element(k) ;
			
			INT i         = 0 ;
			state_seq[i]  = path_ends.element(k) ;
			short int q   = ktable_end.element(k) ;
			pos_seq[i]    = seq_len-1 ;

			while (pos_seq[i]>0)
			{
				//fprintf(stderr,"s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
				state_seq[i+1] = psi.element(pos_seq[i], state_seq[i], q);
				pos_seq[i+1]   = ptable.element(pos_seq[i], state_seq[i], q) ;
				q              = ktable.element(pos_seq[i], state_seq[i], q) ;
				i++ ;
			}
			//fprintf(stderr,"s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
			INT num_states = i+1 ;
			for (i=0; i<num_states;i++)
			{
				my_state_seq[i+k*seq_len] = state_seq[num_states-i-1] ;
			}
			//my_state_seq[num_states+k*seq_len]=-1 ;
		}

	}
}

