#include "DiagnosticScalar.h"

#include <string>
#include <iomanip>

#include "PicParams.h"
#include "DiagParams.h"
#include "SmileiMPI.h"
#include "ElectroMagn.h"

using namespace std;

// constructor
DiagnosticScalar::DiagnosticScalar(PicParams* params, DiagParams* diagParams, SmileiMPI* smpi) :
isMaster(smpi->isMaster()),
cpuSize(smpi->getSize()),
res_time(params->res_time),
every(diagParams->scalar_every),
cell_volume(params->cell_volume),
precision(diagParams->scalar_precision)
{
    if (isMaster) {
        fout.open("scalars.txt");
        if (!fout.is_open()) ERROR("can't open scalar file");
    }
}

void DiagnosticScalar::close() {
    if (isMaster) {
        fout.close();
    }
}

// wrapper of the methods
void DiagnosticScalar::run(int timestep, ElectroMagn* EMfields, vector<Species*>& vecSpecies, SmileiMPI *smpi) {
    EMfields->computePoynting(); // This must be called at each timestep
    if (timestep==0) {
        compute(EMfields,vecSpecies,smpi);
        Energy_time_zero=getScalar("E_tot");
    }
    if (every && timestep % every == 0) {
        compute(EMfields,vecSpecies,smpi);
        write(timestep);
    }
}


// it contains all to manage the communication of data. It is "transparent" to the user.
void DiagnosticScalar::compute (ElectroMagn* EMfields, vector<Species*>& vecSpecies, SmileiMPI *smpi) {
    out_list.clear();

    ///////////////////////////////////////////////////////////////////////////////////////////
    // SPECIES STUFF
    ///////////////////////////////////////////////////////////////////////////////////////////
    double Etot_part=0;    
    for (unsigned int ispec=0; ispec<vecSpecies.size(); ispec++) {
        double charge_tot=0.0;
        double ener_tot=0.0;
        unsigned int nPart=vecSpecies[ispec]->getNbrOfParticles();
        
        if (nPart>0) {
            for (unsigned int iPart=0 ; iPart<nPart; iPart++ ) {
                charge_tot+=(double)vecSpecies[ispec]->particles.charge(iPart);
                ener_tot+=cell_volume*vecSpecies[ispec]->particles.weight(iPart)*(vecSpecies[ispec]->particles.lor_fac(iPart)-1.0);
            }
            ener_tot*=vecSpecies[ispec]->part_mass;
        }
        
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&charge_tot, &charge_tot, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&ener_tot, &ener_tot, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&nPart, &nPart, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);

        if (isMaster) {
            if (nPart!=0) charge_tot /= nPart;
            out_list.push_back(make_pair("Z_"+vecSpecies[ispec]->name_str,charge_tot));
            out_list.push_back(make_pair("E_"+vecSpecies[ispec]->name_str,ener_tot));
            out_list.push_back(make_pair("N_"+vecSpecies[ispec]->name_str,nPart));
            Etot_part+=ener_tot;
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////
    // ELECTROMAGN STUFF
    ///////////////////////////////////////////////////////////////////////////////////////////
    
    
    vector<Field*> fields;
    
    fields.push_back(EMfields->Ex_);
    fields.push_back(EMfields->Ey_);
    fields.push_back(EMfields->Ez_);
    fields.push_back(EMfields->Bx_m);
    fields.push_back(EMfields->By_m);
    fields.push_back(EMfields->Bz_m);
    
    double Etot_fields=0.0;
    
    for (vector<Field*>::iterator field=fields.begin(); field!=fields.end(); field++) {
        
        map<string,val_index> scalars_map;
        
        double Etot=0.0;
        
        vector<unsigned int> iFieldStart(3,0), iFieldEnd(3,1), iFieldGlobalSize(3,1);
        for (unsigned int i=0 ; i<(*field)->isDual_.size() ; i++ ) {
            iFieldStart[i] = EMfields->istart[i][(*field)->isDual(i)];
            iFieldEnd [i] = iFieldStart[i] + EMfields->bufsize[i][(*field)->isDual(i)];
            iFieldGlobalSize [i] = (*field)->dims_[i];
        }
        
        for (unsigned int k=iFieldStart[2]; k<iFieldEnd[2]; k++) {
            for (unsigned int j=iFieldStart[1]; j<iFieldEnd[1]; j++) {
                for (unsigned int i=iFieldStart[0]; i<iFieldEnd[0]; i++) {
                    unsigned int ii=k+ j*iFieldGlobalSize[2] +i*iFieldGlobalSize[1]*iFieldGlobalSize[2];
                    Etot+=pow((**field)(ii),2);
                }
            }
        }
        Etot*=0.5*cell_volume;
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&Etot, &Etot, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        if (isMaster) {
            out_list.push_back(make_pair((*field)->name+"_U",Etot));
            Etot_fields+=Etot;
        }
    }

    // now we add currents and density
    
    fields.push_back(EMfields->Jx_);
    fields.push_back(EMfields->Jy_);
    fields.push_back(EMfields->Jz_);
    fields.push_back(EMfields->rho_);
    
    
    vector<val_index> minis, maxis;
    
    for (vector<Field*>::iterator field=fields.begin(); field!=fields.end(); field++) {
        
        val_index minVal, maxVal;
        
        minVal.val=maxVal.val=(**field)(0);
        minVal.index=maxVal.index=0;
        
        vector<unsigned int> iFieldStart(3,0), iFieldEnd(3,1), iFieldGlobalSize(3,1);
        for (unsigned int i=0 ; i<(*field)->isDual_.size() ; i++ ) {
            iFieldStart[i] = EMfields->istart[i][(*field)->isDual(i)];
            iFieldEnd [i] = iFieldStart[i] + EMfields->bufsize[i][(*field)->isDual(i)];
            iFieldGlobalSize [i] = (*field)->dims_[i];
        }
        for (unsigned int k=iFieldStart[2]; k<iFieldEnd[2]; k++) {
            for (unsigned int j=iFieldStart[1]; j<iFieldEnd[1]; j++) {
                for (unsigned int i=iFieldStart[0]; i<iFieldEnd[0]; i++) {
                    unsigned int ii=k+ j*iFieldGlobalSize[2] +i*iFieldGlobalSize[1]*iFieldGlobalSize[2];
                    if (minVal.val>(**field)(ii)) {
                        minVal.val=(**field)(ii);
                        minVal.index=ii; // rank encoded
                    }
                    if (maxVal.val<(**field)(ii)) {
                        maxVal.val=(**field)(ii);
                        minVal.index=ii; // rank encoded
                    }
                }
            }
        }
        minis.push_back(minVal);
        maxis.push_back(maxVal);
    }
    
    MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&minis[0], &minis[0], minis.size(), MPI_DOUBLE_INT, MPI_MINLOC, 0, MPI_COMM_WORLD);
    MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&maxis[0], &maxis[0], maxis.size(), MPI_DOUBLE_INT, MPI_MAXLOC, 0, MPI_COMM_WORLD);

    if (isMaster) {
        if (minis.size() == maxis.size() && minis.size() == fields.size()) {
            unsigned int i=0;
            for (vector<Field*>::iterator field=fields.begin(); field!=fields.end() && i<minis.size(); field++, i++) {
                
                out_list.push_back(make_pair((*field)->name+"Min",minis[i].val));                
                out_list.push_back(make_pair((*field)->name+"MinCell",minis[i].index));                
                
                out_list.push_back(make_pair((*field)->name+"Max",maxis[i].val));
                out_list.push_back(make_pair((*field)->name+"MaxCell",maxis[i].index));                
            
            }
        }
        
    }

    //poynting stuff
    double poyTot=0.0;
    for (unsigned int j=0; j<2;j++) {
        for (unsigned int i=0; i<EMfields->poynting[j].size();i++) {
            
            double poy=EMfields->poynting[j][i];

            MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&poy, &poy, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
            
            if (isMaster) {
                stringstream s;
                s << "Poy_" << (j==0?"inf":"sup") << "_" << (i==0?"x":(i==1?"y":"z"));
                out_list.push_back(make_pair(s.str(),poy));              
                poyTot+=poy;
                
            }
            
        }
    }
    
    if (isMaster) {

        double Total_Energy=Etot_part+Etot_fields;

        double Energy_Balance=Total_Energy-(Energy_time_zero+poyTot);
        double Energy_Bal_norm=Energy_Balance/Total_Energy;
        
        out_list.insert(out_list.begin(),make_pair("Poynting",poyTot)); 
        out_list.insert(out_list.begin(),make_pair("EFields",Etot_fields));
        out_list.insert(out_list.begin(),make_pair("Eparticles",Etot_part));
        out_list.insert(out_list.begin(),make_pair("Etot",Total_Energy));
        out_list.insert(out_list.begin(),make_pair("Ebalance",Energy_Balance));
        out_list.insert(out_list.begin(),make_pair("Ebal_norm",Energy_Bal_norm));
    }
    
        
}

void DiagnosticScalar::write(int itime) {
    if(isMaster) {
        fout << std::scientific;
        fout.precision(precision);
        if (fout.tellp()==ifstream::pos_type(0)) {
            fout << "# " << 1 << " time" << endl;
            unsigned int i=2;
            for(vector<pair<string,double> >::iterator iter = out_list.begin(); iter !=out_list.end(); iter++) {
                fout << "# " << i << " " << (*iter).first << endl;
                i++;
            }

            fout << "#\n#" << setw(precision+9) << "time";
            for(vector<pair<string,double> >::iterator iter = out_list.begin(); iter !=out_list.end(); iter++) {
                fout << setw(precision+9) << (*iter).first;
            }
            fout << endl;
        }
        fout << setw(precision+9) << itime/res_time;
        for(vector<pair<string,double> >::iterator iter = out_list.begin(); iter !=out_list.end(); iter++) {
            fout << setw(precision+9) << (*iter).second;
        }
        fout << endl;
    }
}

double DiagnosticScalar::getScalar(string name){
    for (unsigned int i=0; i< out_list.size(); i++) {
        if (out_list[i].first==name) {
            return out_list[i].second;
        }
    }    
    DEBUG("key not found " << name);
    return 0.0;
}

