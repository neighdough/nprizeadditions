/*
########################################################################
#  Netflix Prize Tools
#  Copyright (C) 2009 Greg Bildson
#  http://code.google.com/p/nprizeadditions/
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation version 2.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
########################################################################
*/
/*   rbm.c
     This is a pure restricted boltzmann machine for use in running against the netflix prize dataset.

     With 100 hidden variables, it should achieve a probe RMSE of 0.918197.
     With 200 hidden variables, it should achieve a probe RMSE of 0.916576.
     The program requires data setup as specified in the original nprize code here:
       http://code.google.com/p/nprize/
     Helpful documentation of the RBM algorithm, can be found in these locations:
       http://www.cs.utoronto.ca/~hinton/absps/netflixICML.pdf
       http://www.cs.toronto.edu/~rsalakhu/code_AIS/rbm.m
       http://www.iro.umontreal.ca/~lisa/twiki/bin/view.cgi/Public/DBNPseudoCode
       http://imonad.com/blog/2008/10/restricted-boltzmann-machine/
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "basic.h"
#include "netflix.h"
#include "utest.h"
#include "weight.h"

// Hard coded for 100 hidden variables.  This can adapt to 200 hidden.  See code at end.
#define TOTAL_FEATURES  100  
#define SOFTMAX         5
#define epsilonw        0.001   // Learning rate for weights
#define epsilonvb       0.008   // Learning rate for biases of visible units
#define epsilonhb       0.0006  // Learning rate for biases of hidden units
#define weightcost      0.0001
#define momentum        0.8  
#define finalmomentum   0.9      


// vishid are the weights.
double vishid[NMOVIES][SOFTMAX][TOTAL_FEATURES];
double visbiases[NMOVIES][SOFTMAX];
double hidbiases[TOTAL_FEATURES];
double CDpos[NMOVIES][SOFTMAX][TOTAL_FEATURES];
double CDneg[NMOVIES][SOFTMAX][TOTAL_FEATURES];
double CDinc[NMOVIES][SOFTMAX][TOTAL_FEATURES];

double poshidprobs[TOTAL_FEATURES];
char   poshidstates[TOTAL_FEATURES]; 
char   curposhidstates[TOTAL_FEATURES]; 
double poshidact[TOTAL_FEATURES];
double neghidact[TOTAL_FEATURES];
double neghidprobs[TOTAL_FEATURES];
char   neghidstates[TOTAL_FEATURES]; 
double hidbiasinc[TOTAL_FEATURES];

double nvp2[NMOVIES][SOFTMAX];
double negvisprobs[NMOVIES][SOFTMAX];
char   negvissoftmax[NMOVIES]; 
double posvisact[NMOVIES][SOFTMAX];
double negvisact[NMOVIES][SOFTMAX];
double visbiasinc[NMOVIES][SOFTMAX];

unsigned int moviercount[SOFTMAX*NMOVIES];
unsigned int moviecount[NMOVIES];


#define E  (0.00002) // stop condition
int score_argv(char **argv) {return 0;}

void score_setup() {
    int i,u,m, j;

    for (m=0; m<NMOVIES; m++) {
        moviercount[m*SOFTMAX+0] = 0;
        moviercount[m*SOFTMAX+1] = 0;
        moviercount[m*SOFTMAX+2] = 0;
        moviercount[m*SOFTMAX+3] = 0;
        moviercount[m*SOFTMAX+4] = 0;
    }
    for(u=0;u<NUSERS;u++) {
        int base0=useridx[u][0];
        int d0=UNTRAIN(u);

        // For all rated movies
        for(j=0;j<d0;j++) {
            int m=userent[base0+j]&USER_MOVIEMASK;
            int r=(userent[base0+j]>>USER_LMOVIEMASK)&7;
            moviercount[m*SOFTMAX+r]++;
        }
    }
}


void recordErrors() {
    int u,h,f, j, i;
    for(u=0;u<NUSERS;u++) {

        // Zero out the probability accumulator
        ZERO(negvisprobs);

        //
        // Perform a training iteration on pure probabilities up to visible node reconstruction

        int base0=useridx[u][0];
        int d0=UNTRAIN(u);
        int dall=UNALL(u);

        // For all rated movies, accumulate contributions to hidden units
        double sumW[TOTAL_FEATURES];
        ZERO(sumW);
        for(j=0;j<d0;j++) {
            int m=userent[base0+j]&USER_MOVIEMASK;

            // 1. get one data point from data set.
            // 2. use values of this data point to set state of visible neurons Si
            int r=(userent[base0+j]>>USER_LMOVIEMASK)&7;

            // for all hidden units h:
            for(h=0;h<TOTAL_FEATURES;h++) {
                // sum_j(W[i][j] * v[0][j]))
                sumW[h]  += vishid[m][r][h];
            }
        }

        // Compute the hidden probabilities
        for(h=0;h<TOTAL_FEATURES;h++) {

            // 3. compute Sj for each hidden neuron based on formula above and states of visible neurons Si
            // compute Q(h[0][i] = 1 | v[0]) # for binomial units, sigmoid(b[i] + sum_j(W[i][j] * v[0][j]))
            poshidprobs[h]  = 1.0/(1.0 + exp(-sumW[h] - hidbiases[h]));
        }

        // 5. on visible neurons compute Si using the Sj computed in step3. This is known as reconstruction
        // for all visible units j:
        int r;
        int count = dall;
        for(j=0;j<count;j++) {
            int m=userent[base0+j]&USER_MOVIEMASK;
            for(h=0;h<TOTAL_FEATURES;h++) {
                for(r=0;r<SOFTMAX;r++) 
                    negvisprobs[m][r]  += poshidprobs[h] * vishid[m][r][h];
            }

            // compute P(v[1][j] = 1 | h[0]) # for binomial units, sigmoid(c[j] + sum_i(W[i][j] * h[0][i]))
            negvisprobs[m][0]  = 1./(1 + exp(-negvisprobs[m][0] - visbiases[m][0]));
            negvisprobs[m][1]  = 1./(1 + exp(-negvisprobs[m][1] - visbiases[m][1]));
            negvisprobs[m][2]  = 1./(1 + exp(-negvisprobs[m][2] - visbiases[m][2]));
            negvisprobs[m][3]  = 1./(1 + exp(-negvisprobs[m][3] - visbiases[m][3]));
            negvisprobs[m][4]  = 1./(1 + exp(-negvisprobs[m][4] - visbiases[m][4]));

            // Normalize probabilities
            double tsum  = 
              negvisprobs[m][0] +
              negvisprobs[m][1] +
              negvisprobs[m][2] +
              negvisprobs[m][3] +
              negvisprobs[m][4];
            if ( tsum != 0 ) {
                negvisprobs[m][0]  /= tsum;
                negvisprobs[m][1]  /= tsum;
                negvisprobs[m][2]  /= tsum;
                negvisprobs[m][3]  /= tsum;
                negvisprobs[m][4]  /= tsum;
            }
        }

        // Compute and save error residuals
        for(i=0; i<dall;i++) {
            int m=userent[base0+i]&USER_MOVIEMASK;
            int r=(userent[base0+i]>>USER_LMOVIEMASK)&7;
            double expectedV = negvisprobs[m][1] + 2.0 * negvisprobs[m][2] + 3.0 * negvisprobs[m][3] + 4.0 * negvisprobs[m][4];
            double vdelta = (((double)r)-expectedV);
            err[base0+i] = vdelta;
        }
    }
}

int score_train(int loop) {
    if (loop == 0)
        return doAllFeatures();
    
    return 1;
}

double randn() {
    return (rand()/(double)(RAND_MAX));
}

int doAllFeatures() {
    /* Initial weights */
    int i, j, h;
    for (j=0; j<NMOVIES; j++) {
        for (i=0; i<TOTAL_FEATURES; i++) {
            vishid[j][0][i] = 0.02 * randn() - 0.01; // Normal Distribution
            vishid[j][1][i] = 0.02 * randn() - 0.01; // Normal Distribution
            vishid[j][2][i] = 0.02 * randn() - 0.01; // Normal Distribution
            vishid[j][3][i] = 0.02 * randn() - 0.01; // Normal Distribution
            vishid[j][4][i] = 0.02 * randn() - 0.01; // Normal Distribution
        }
    }

    /* Initial biases */
    for(i=0;i<TOTAL_FEATURES;i++) {
        hidbiases[i]=0.0;
    }
    for (j=0; j<NMOVIES; j++) {
        unsigned int mtot = moviercount[j*SOFTMAX+0] + moviercount[j*SOFTMAX+1] + moviercount[j*SOFTMAX+2] + moviercount[j*SOFTMAX+3] + moviercount[j*SOFTMAX+4];
        for (i=0; i<SOFTMAX; i++) {
            visbiases[j][i] = log( ((double)moviercount[j*SOFTMAX+i]) / ((double) mtot) );
        }
    }

    /* Optimize current feature */
    double nrmse=2., last_rmse=10.;
    double prmse = 0, last_prmse=0;
    double s;
    int n;
    int loopcount=0;
    double EpsilonW  = epsilonw;
    double EpsilonVB = epsilonvb;
    double EpsilonHB = epsilonhb;
    double Momentum  = momentum;
    ZERO(CDinc);
    ZERO(visbiasinc);
    ZERO(hidbiasinc);
    int tSteps = 1;

    // Iterate through the model while the RMSE is decreasing 
    //while ( ((nrmse < (last_rmse-E) && prmse<last_prmse) || loopcount < 14) && loopcount < 80  )  {
    while ( ((nrmse < (last_rmse-E) ) || loopcount < 14) && loopcount < 80  )  {

        if ( loopcount >= 10 )
            tSteps = 3 + (loopcount - 10)/5;

        last_rmse=nrmse;
        last_prmse=prmse;
        clock_t t0=clock();
        loopcount++;
        int ntrain = 0;
        nrmse = 0.0;
        s  = 0.0;
        n = 0;

        if ( loopcount > 5 )
            Momentum = finalmomentum;

        //* CDpos =0, CDneg=0 (matrices)
        ZERO(CDpos);
        ZERO(CDneg);
        ZERO(poshidact);
        ZERO(neghidact);
        ZERO(posvisact);
        ZERO(negvisact);
        ZERO(moviecount);

        int u,m, f;
        for(u=0;u<NUSERS;u++) {

            //* Clear summations for probabilities
            ZERO(negvisprobs);
            ZERO(nvp2);

            //* perform steps 1 to 8
            int base0=useridx[u][0];
            int d0=UNTRAIN(u);
            int dall=UNALL(u);

            // For all rated movies, accumulate contributions to hidden units
            double sumW[TOTAL_FEATURES];
            ZERO(sumW);
            for(j=0;j<d0;j++) {
                int m=userent[base0+j]&USER_MOVIEMASK;
                moviecount[m]++;

                // 1. get one data point from data set.
                // 2. use values of this data point to set state of visible neurons Si
                int r=(userent[base0+j]>>USER_LMOVIEMASK)&7;

                // Add to the bias contribution for set visible units
                posvisact[m][r] += 1.0;
 
                // for all hidden units h:
                for(h=0;h<TOTAL_FEATURES;h++) {
                    // sum_j(W[i][j] * v[0][j]))
                    sumW[h]  += vishid[m][r][h];
                }
            }

            // Sample the hidden units state after computing probabilities
            for(h=0;h<TOTAL_FEATURES;h++) {

                // 3. compute Sj for each hidden neuron based on formula above and states of visible neurons Si
                // poshidprobs[h] = 1./(1 + exp(-V*vishid - hidbiases);
                // compute Q(h[0][i] = 1 | v[0]) # for binomial units, sigmoid(b[i] + sum_j(W[i][j] * v[0][j]))
                poshidprobs[h]  = 1.0/(1.0 + exp(-sumW[h] - hidbiases[h]));

                // sample h[0][i] from Q(h[0][i] = 1 | v[0])
                if  ( poshidprobs[h] >  (rand()/(double)(RAND_MAX)) ) {
                    poshidstates[h]=1;
                    poshidact[h] += 1.0;
                } else {
                    poshidstates[h]=0;
                }
            }

            // Load up a copy of poshidstates for use in loop
            for ( h=0; h < TOTAL_FEATURES; h++ ) 
                curposhidstates[h] = poshidstates[h];

            // Make T Contrastive Divergence steps
            int stepT = 0;
            do {
                // Determine if this is the last pass through this loop
                int finalTStep = (stepT+1 >= tSteps);
                
                // 5. on visible neurons compute Si using the Sj computed in step3. This is known as reconstruction
                // for all visible units j:
                int r;
                int count = d0;
                count += useridx[u][2];  // too compute probe errors
                for(j=0;j<count;j++) {
                    int m=userent[base0+j]&USER_MOVIEMASK;
                    for(h=0;h<TOTAL_FEATURES;h++) {
                        // Accumulate Weight values for sampled hidden states == 1
                        if ( curposhidstates[h] == 1 ) {
                            for(r=0;r<SOFTMAX;r++) {
                                negvisprobs[m][r]  += vishid[m][r][h];
                            }
                        }

                        // Compute more accurate probabilites for RMSE reporting
                        if ( stepT == 0 ) {  
                            for(r=0;r<SOFTMAX;r++) 
                                nvp2[m][r] += poshidprobs[h] * vishid[m][r][h];
                        }
                    }

                    // compute P(v[1][j] = 1 | h[0]) # for binomial units, sigmoid(c[j] + sum_i(W[i][j] * h[0][i]))
                    // Softmax elements are handled individually here
                    negvisprobs[m][0]  = 1./(1 + exp(-negvisprobs[m][0] - visbiases[m][0]));
                    negvisprobs[m][1]  = 1./(1 + exp(-negvisprobs[m][1] - visbiases[m][1]));
                    negvisprobs[m][2]  = 1./(1 + exp(-negvisprobs[m][2] - visbiases[m][2]));
                    negvisprobs[m][3]  = 1./(1 + exp(-negvisprobs[m][3] - visbiases[m][3]));
                    negvisprobs[m][4]  = 1./(1 + exp(-negvisprobs[m][4] - visbiases[m][4]));

                    // Normalize probabilities
                    double tsum  = 
                      negvisprobs[m][0] +
                      negvisprobs[m][1] +
                      negvisprobs[m][2] +
                      negvisprobs[m][3] +
                      negvisprobs[m][4];
                    if ( tsum != 0 ) {
                        negvisprobs[m][0]  /= tsum;
                        negvisprobs[m][1]  /= tsum;
                        negvisprobs[m][2]  /= tsum;
                        negvisprobs[m][3]  /= tsum;
                        negvisprobs[m][4]  /= tsum;
                    }
                    // Compute and Normalize more accurate RMSE reporting probabilities
                    if ( stepT == 0) {
                        nvp2[m][0]  = 1./(1 + exp(-nvp2[m][0] - visbiases[m][0]));
                        nvp2[m][1]  = 1./(1 + exp(-nvp2[m][1] - visbiases[m][1]));
                        nvp2[m][2]  = 1./(1 + exp(-nvp2[m][2] - visbiases[m][2]));
                        nvp2[m][3]  = 1./(1 + exp(-nvp2[m][3] - visbiases[m][3]));
                        nvp2[m][4]  = 1./(1 + exp(-nvp2[m][4] - visbiases[m][4]));
                        double tsum2  = 
                          nvp2[m][0] +
                          nvp2[m][1] +
                          nvp2[m][2] +
                          nvp2[m][3] +
                          nvp2[m][4];
                        if ( tsum2 != 0 ) {
                            nvp2[m][0]  /= tsum2;
                            nvp2[m][1]  /= tsum2;
                            nvp2[m][2]  /= tsum2;
                            nvp2[m][3]  /= tsum2;
                            nvp2[m][4]  /= tsum2;
                        }
                    }

                    // sample v[1][j] from P(v[1][j] = 1 | h[0])
                    double randval = (rand()/(double)(RAND_MAX));
                    if ( (randval -= negvisprobs[m][0]) <= 0.0 )
                        negvissoftmax[m] = 0;
                    else if ( (randval -= negvisprobs[m][1]) <= 0.0 )
                        negvissoftmax[m] = 1;
                    else if ( (randval -= negvisprobs[m][2]) <= 0.0 )
                        negvissoftmax[m] = 2;
                    else if ( (randval -= negvisprobs[m][3]) <= 0.0 )
                        negvissoftmax[m] = 3;
                    else //if ( (randval -= negvisprobs[m][4]) <= 0.0 )
                        negvissoftmax[m] = 4;

                    // if in training data then train on it
                    if ( j < d0 && finalTStep )  
                        negvisact[m][negvissoftmax[m]] += 1.0;
                }


                // 6. compute state of hidden neurons Sj again using Si from 5 step.
                // For all rated movies accumulate contributions to hidden units from sampled visible units
                ZERO(sumW);
                for(j=0;j<d0;j++) {
                    int m=userent[base0+j]&USER_MOVIEMASK;
     
                    // for all hidden units h:
                    for(h=0;h<TOTAL_FEATURES;h++) {
                        sumW[h]  += vishid[m][negvissoftmax[m]][h];
                    }
                }
                // for all hidden units h:
                for(h=0;h<TOTAL_FEATURES;h++) {
                    // compute Q(h[1][i] = 1 | v[1]) # for binomial units, sigmoid(b[i] + sum_j(W[i][j] * v[1][j]))
                    neghidprobs[h]  = 1./(1 + exp(-sumW[h] - hidbiases[h]));

                    // Sample the hidden units state again.
                    if  ( neghidprobs[h] >  (rand()/(double)(RAND_MAX)) ) {
                        neghidstates[h]=1;
                        if ( finalTStep )
                            neghidact[h] += 1.0;
                    } else {
                        neghidstates[h]=0;
                    }
                }

                // Compute error rmse and prmse before we start iterating on T
                if ( stepT == 0 ) {

                    // Compute rmse on training data
                    for(j=0;j<d0;j++) {
                        int m=userent[base0+j]&USER_MOVIEMASK;
                        int r=(userent[base0+j]>>USER_LMOVIEMASK)&7;
         
                        //# Compute some error function like sum of squared difference between Si in 1) and Si in 5)
                        double expectedV = nvp2[m][1] + 2.0 * nvp2[m][2] + 3.0 * nvp2[m][3] + 4.0 * nvp2[m][4];
                        double vdelta = (((double)r)-expectedV);
                        nrmse += (vdelta * vdelta);
                    }
                    ntrain+=d0;

                    // Sum up probe rmse
                    int base=useridx[u][0];
                    for(i=1;i<2;i++) base+=useridx[u][i];
                    int d=useridx[u][2];
                    for(i=0; i<d;i++) {
                        int m=userent[base+i]&USER_MOVIEMASK;
                        int r=(userent[base+i]>>USER_LMOVIEMASK)&7;
                        //# Compute some error function like sum of squared difference between Si in 1) and Si in 5)
                        double expectedV = nvp2[m][1] + 2.0 * nvp2[m][2] + 3.0 * nvp2[m][3] + 4.0 * nvp2[m][4];
                        double vdelta = (((double)r)-expectedV);
                        s+=vdelta*vdelta;
                    }
                    n+=d;
                }

                // If looping again, load the curposvisstates
                if ( !finalTStep ) {
                    for ( h=0; h < TOTAL_FEATURES; h++ ) 
                        curposhidstates[h] = neghidstates[h];
                    ZERO(negvisprobs);
                }

              // 8. repeating multiple times steps 5,6 and 7 compute (Si.Sj)n. Where n is small number and can 
              //    increase with learning steps to achieve better accuracy.

            } while ( ++stepT < tSteps );

            // Accumulate contrastive divergence contributions for (Si.Sj)0 and (Si.Sj)T
            for(j=0;j<d0;j++) {
                int m=userent[base0+j]&USER_MOVIEMASK;
                int r=(userent[base0+j]>>USER_LMOVIEMASK)&7;
 
                // for all hidden units h:
                for(h=0;h<TOTAL_FEATURES;h++) {
                    if ( poshidstates[h] == 1 ) {
                        // 4. now Si and Sj values can be used to compute (Si.Sj)0  here () means just values not average
                        //* accumulate CDpos = CDpos + (Si.Sj)0
                        CDpos[m][r][h] += 1.0;
                    }

                    // 7. now use Si and Sj to compute (Si.Sj)1 (fig.3)
                    CDneg[m][negvissoftmax[m]][h] += (double)neghidstates[h];
                }
            }

            // Update weights and biases after batch
            //
            int bsize = 100;
            if ( ((u+1) % bsize) == 0 || (u+1) == NUSERS ) {
                int numcases = u % bsize;
                numcases++;

                // Update weights
                for(m=0;m<NMOVIES;m++) {
                    if ( moviecount[m] == 0 ) continue;

                    // for all hidden units h:
                    for(h=0;h<TOTAL_FEATURES;h++) {
                        // for all softmax
                        int rr;
                        for(rr=0;rr<SOFTMAX;rr++) {
                            //# At the end compute average of CDpos and CDneg by dividing them by number of data points.
                            //# Compute CD = < Si.Sj >0  < Si.Sj >n = CDpos  CDneg
                            double CDp = CDpos[m][rr][h];
                            double CDn = CDneg[m][rr][h];
                            if ( CDp != 0.0 || CDn != 0.0 ) {
                                CDp /= ((double)moviecount[m]);
                                CDn /= ((double)moviecount[m]);

                                // W += epsilon * (h[0] * v[0]' - Q(h[1][.] = 1 | v[1]) * v[1]')
                                //# Update weights and biases W = W + alpha*CD (biases are just weights to neurons that stay always 1.0)
                                //e.g between data and reconstruction.
                                CDinc[m][rr][h] = Momentum * CDinc[m][rr][h] + EpsilonW * ((CDp - CDn) - weightcost * vishid[m][rr][h]);
                                vishid[m][rr][h] += CDinc[m][rr][h];
                            } 
                        }
                    }

                    // Update visible softmax biases
                    // c += epsilon * (v[0] - v[1])$
                    // for all softmax
                    int rr;
                    for(rr=0;rr<SOFTMAX;rr++) {
                        if ( posvisact[m][rr] != 0.0 || negvisact[m][rr] != 0.0 ) {
                            posvisact[m][rr] /= ((double)moviecount[m]);
                            negvisact[m][rr] /= ((double)moviecount[m]);
                            visbiasinc[m][rr] = Momentum * visbiasinc[m][rr] + EpsilonVB * ((posvisact[m][rr] - negvisact[m][rr]));
                            //visbiasinc[m][rr] = Momentum * visbiasinc[m][rr] + EpsilonVB * ((posvisact[m][rr] - negvisact[m][rr]) - weightcost * visbiases[m][rr]);
                            visbiases[m][rr]  += visbiasinc[m][rr];
                        }
                    }
                }

                
                // Update hidden biases
                // b += epsilon * (h[0] - Q(h[1][.] = 1 | v[1]))
                for(h=0;h<TOTAL_FEATURES;h++) {
                    if ( poshidact[h]  != 0.0 || neghidact[h]  != 0.0 ) {
                        poshidact[h]  /= ((double)(numcases));
                        neghidact[h]  /= ((double)(numcases));
                        hidbiasinc[h] = Momentum * hidbiasinc[h] + EpsilonHB * ((poshidact[h] - neghidact[h]));
                        //hidbiasinc[h] = Momentum * hidbiasinc[h] + EpsilonHB * ((poshidact[h] - neghidact[h]) - weightcost * hidbiases[h]);
                        hidbiases[h]  += hidbiasinc[h];
                    }
                }
                ZERO(CDpos);
                ZERO(CDneg);
                ZERO(poshidact);
                ZERO(neghidact);
                ZERO(posvisact);
                ZERO(negvisact);
                ZERO(moviecount);
            }
        }

        nrmse=sqrt(nrmse/ntrain);
        prmse = sqrt(s/n);
        
        lg("%f\t%f\t%f\n",nrmse,prmse,(clock()-t0)/(double)CLOCKS_PER_SEC);

        if ( TOTAL_FEATURES == 200 ) {
            if ( loopcount > 6 ) {
                EpsilonW  *= 0.90;
                EpsilonVB *= 0.90;
                EpsilonHB *= 0.90;
            } else if ( loopcount > 5 ) {  // With 200 hidden variables, you need to slow things down a little more
                EpsilonW  *= 0.50;         // This could probably use some more optimization
                EpsilonVB *= 0.50;
                EpsilonHB *= 0.50;
            } else if ( loopcount > 2 ) {
                EpsilonW  *= 0.70;
                EpsilonVB *= 0.70;
                EpsilonHB *= 0.70;
            }
        } else {  // The 100 hidden variable case
            if ( loopcount > 8 ) {
                EpsilonW  *= 0.92;
                EpsilonVB *= 0.92;
                EpsilonHB *= 0.92;
            } else if ( loopcount > 6 ) {
                EpsilonW  *= 0.90;
                EpsilonVB *= 0.90;
                EpsilonHB *= 0.90;
            } else if ( loopcount > 2 ) {
                EpsilonW  *= 0.78;
                EpsilonVB *= 0.78;
                EpsilonHB *= 0.78;
            }
        }
    }
    
    /* Perform a final iteration in which the errors are clipped and stored */
    recordErrors();
    
    //if(save_model) {
        //dappend_bin(fnameV,sV,NMOVIES);
        //dappend_bin(fnameU,sU,NUSERS);
    //}
    
    return 1;
}
