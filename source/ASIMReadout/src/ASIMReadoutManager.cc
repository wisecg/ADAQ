/////////////////////////////////////////////////////////////////////////////////
//
// name: ASIMReadoutManager.cc
// date: 23 Dec 14
// auth: Zach Hartwig
// mail: hartwig@psfc.mit.edu
// 
// desc: The ASIMReadoutManager class provides a framework for reading
//       out simulated detector data into a standardized format ROOT
//       file (called an ASIM file). THe primary purpose is to provide
//       a standardized, flexible method for high efficiency detector
//       data persistency and post-simulation data analysis using
//       software tools (e.g. the ADAQAnalysis program). The class
//       utilizes the ADAQSimulationEvent and ADAQSimultionRun classes
//       for containerized data storage with ROOT TCollection objects
//       to facilitate data storage. The class is primarily developed
//       for use with Geant4 "Sensitive Detectors" but in principle
//       could be used by any Monte Carlo simulation that can be
//       integrated with the ROOT toolkit.
//
/////////////////////////////////////////////////////////////////////////////////

#include <TChain.h>

#include <iostream>
#include <sstream>
#include <unistd.h>
#include <cstdlib>

#include "ASIMReadoutManager.hh"

ASIMReadoutManager::ASIMReadoutManager()
  : ASIMFile(new TFile), ASIMFileName(""), ASIMFileOpen(false), 
    MPI_Rank(0), MPI_Size(0),
    EventTreeList(new TList), RunList(new TList)
{ PopulateMetadata(); }


ASIMReadoutManager::~ASIMReadoutManager()
{
  delete RunList;
  delete EventTreeList;
  delete ASIMFile;
}


//////////////////////////////////////
// Methods for ASIM file management //
//////////////////////////////////////

void ASIMReadoutManager::CreateSequentialFile(std::string Name)
{
  if(ASIMFileOpen){
    std::cout << "ASIMReadoutManager::CreateSequentialFile():\n"
	      << "  An ASIM file is presently open for data output! A new ASIM file\n"
	      << "  cannot be opened until the existing ASIM file is written to disk\n"
	      << "  and closed. Nothing to be done.\n"
	      << std::endl;
    
    return;
  }
  
  ASIMFileName = Name;
  
  // Recreate a new ROOT TFile for the ASIM file
  if(ASIMFile) delete ASIMFile;
  ASIMFile = new TFile(ASIMFileName, "recreate");
  
  ASIMFileOpen = true;
}


void ASIMReadoutManager::CreateParallelFile(std::string Name,
					    Int_t Rank,
					    Int_t Size)
{
  if(ASIMFileOpen){
    std::cout << "ASIMReadoutManager::CreateParallelFile():\n"
	      << "  An ASIM file is presently open for data output! A new ASIM file\n"
	      << "  cannot be opened until the existing ASIM file is written to disk\n"
	      << "  and closed. Nothing to be done.\n"
	      << std::endl;
    
    return;
  }
  
  // Set MPI rank and size class members
  MPI_Rank = Rank;
  MPI_Size = Size;

  // Generate names for all slaves ASIM files based on rank
  std::stringstream SS;
  SS << Name << ".slave" << Rank;
  ASIMFileName = SS.str();
  GenerateSlaveFileNames();
  
  // Recreate slave ASIM files
  if(ASIMFile) delete ASIMFile;
  ASIMFile = new TFile(ASIMFileName, "recreate");
  
  ASIMFileOpen = true;
}


void ASIMReadoutManager::GenerateSlaveFileNames()
{
  if(ASIMFileOpen)
    return;
  
  SlaveFileNames.clear();
  for(Int_t rank=0; rank<MPI_Size; rank++){

    std::string Name = ASIMFileName.Data();
    std::stringstream SS;
    
    size_t pos = Name.find("slave");
    if(pos != std::string::npos){
      SS << Name.substr(0,pos) << "slave" << rank;
      SlaveFileNames.push_back((TString)SS.str());
    }
    else
      // Should generate exception
      {}
  }
}


void ASIMReadoutManager::WriteSequentialFile()
{
  if(!ASIMFileOpen)
    return;


  // Write the metadata
  WriteMetadata();
  
  // Write out each individual tree in the EventTreeList
  EventTreeList->Write();
  
  // Write out each of the run objects in the RunList
  WriteRuns();

  ASIMFile->Close();

  ASIMFileOpen = false;
}


void ASIMReadoutManager::WriteParallelFile()
{
  if(!ASIMFileOpen)
    return;

  std::string Name = ASIMFileName.Data();

  // All slaves should write out the event TTrees contained on each
  // slave node to a node-specific ROOT file and then close the file
  EventTreeList->Write();
  ASIMFile->Close();

  // Only a single process (the master) should handle the aggregation
  // of slave ROOT files containing the event treesinto a single
  // master ASIM file that contains all event- and run-level data

  if(MPI_Rank == 0){
    // Create the master ASIM file name
    TString FinalFileName = Name.substr(0, Name.find(".slave"));
    
    // Create a TChain for all existing event TTrees by iterating over
    // the TTree names stored in the std::map. Because the slave
    // ASIMFiles have been written and closed at this point, the
    // TTrees have been purged from ROOT memory so it's easier to just
    // grab the names from the std::map.

    std::map<Int_t, std::string>::iterator It0;
    Int_t Index = 0;
    for(It0 = EventTreeNameMap.begin(); It0!=EventTreeNameMap.end(); It0++){

      TChain *EventTreeChain = new TChain(It0->second.c_str());

      std::vector<TString>::iterator It1;
      for(It1 = SlaveFileNames.begin(); It1 != SlaveFileNames.end(); It1++)
	EventTreeChain->Add((*It1));
      
      // The following method enables multiple TChains to be written
      // to the final ASIM ROOT file without overwriting each other
      
      if(Index == 0)
	EventTreeChain->Merge(FinalFileName);
      else{
	TFile *FinalFile = new TFile(FinalFileName, "update");
	EventTreeChain->Merge(FinalFile, 0);
      }
      
      delete EventTreeChain;

      Index++;
    }

    // Update the master ASIM file with file metadata and run-level
    // data, which is not dependent on the slave ASIM files
    TFile *FinalFile = new TFile(FinalFileName, "update");
    WriteMetadata();
    WriteRuns();

    // Write, close, and delete the master ASIM TFile object
    FinalFile->Close();
    delete FinalFile;
      
    // Delete the node-specific ASIM files from the operating system

    std::vector<TString>::iterator It2;
    for(It2 = SlaveFileNames.begin(); It2 != SlaveFileNames.end(); It2++){
      std::string FileName = (*It2).Data();
      std::string RemoveSlaveFileCmd = "rm -f " + FileName;
      system(RemoveSlaveFileCmd.c_str());
    }
  }
  ASIMFileOpen = false;
}


void ASIMReadoutManager::PopulateMetadata()
{
  char Host[128], User[128];
  gethostname(Host, sizeof Host);
  getlogin_r(User, sizeof User);

  MachineName = new TObjString(Host);
  MachineUser = new TObjString(User);

  time_t RawTime;
  time(&RawTime);
  FileDate = new TObjString(ctime(&RawTime));
  FileVersion = new TObjString("1.0");
  FileComment = new TObjString("");
}


void ASIMReadoutManager::WriteMetadata()
{
  MachineName->Write("MachineName");
  MachineUser->Write("MachineUser");
  FileDate->Write("FileDate");
  FileVersion->Write("FileVersion");
  FileComment->Write("FileComment");
}


///////////////////////////////////
// Methods for ASIMEvent objects //
///////////////////////////////////

ASIMEvent *ASIMReadoutManager::CreateEventTree(Int_t ID,
					       TString Name,
					       TString Desc)
{
  ASIMEvent *Event = new ASIMEvent;

  TTree *T = new TTree(Name, Desc);
  T->Branch("ASIMEventBranch", "ASIMEvent", Event, 32000, 99);
  
  EventTreeIDMap[(std::string)Name] = ID;
  EventTreeNameMap[ID] = (std::string)Name;
  
  EventTreeList->Add(T);

  return Event;
}

void ASIMReadoutManager::AddEventTree(Int_t ID,
				      TTree *T)
{
  EventTreeIDMap[T->GetName()] = ID;
  EventTreeNameMap[ID] = T->GetName();
  
  EventTreeList->Add(T);
}


void ASIMReadoutManager::RemoveEventTree(std::string Name)
{
  TTree *T = (TTree *)EventTreeList->FindObject(Name.c_str());
  EventTreeList->Remove(T);
}


void ASIMReadoutManager::RemoveEventTree(Int_t ID)
{ RemoveEventTree(EventTreeNameMap[ID]); }


TTree *ASIMReadoutManager::GetEventTree(std::string Name)
{
  TTree *T = (TTree *)EventTreeList->FindObject(Name.c_str());
  return T;
}


TTree *ASIMReadoutManager::GetEventTree(Int_t ID)
{ return GetEventTree(EventTreeNameMap[ID]); }


void ASIMReadoutManager::ListEventTrees()
{
  TIter It(EventTreeList);
  TTree *T;
  while((T = (TTree *)It.Next())){
    std::cout << "Tree name: " << T->GetName() << std::endl;
  }
}


void ASIMReadoutManager::WriteEventTrees()
{
  if(!ASIMFileOpen)
    return;
  
  TIter It(EventTreeList);
  TTree *T;
  while((T = (TTree *)It.Next()))
    T->Write();
}


Int_t ASIMReadoutManager::GetNumberOfEventTrees()
{ return EventTreeList->GetSize(); }


Int_t ASIMReadoutManager::GetEventTreeID(std::string Name)
{ return EventTreeIDMap[Name]; }


std::string ASIMReadoutManager::GetEventTreeName(Int_t ID)
{ return EventTreeNameMap[ID]; }


/////////////////////////////////
// Methods for ASIMRun objects //
/////////////////////////////////


void ASIMReadoutManager::AddRun(ASIMRun *Run)
{ RunList->Add(Run); }


ASIMRun *ASIMReadoutManager::GetRun(Int_t ID)
{ return (ASIMRun *)RunList->At(ID); }


Int_t ASIMReadoutManager::GetNumberOfRuns()
{ return RunList->GetSize(); }


void ASIMReadoutManager::ListRuns()
{
  TIter It(RunList);
  ASIMRun *R;
  while((R = (ASIMRun *)It.Next())){
  }
}

void ASIMReadoutManager::WriteRuns()
{
  if(!ASIMFileOpen)
    return;

  TIter It(RunList);
  ASIMRun *R;
  while((R = (ASIMRun *)It.Next())){
    std::stringstream SS;
    SS << "Run" << R->GetRunID();
    TString Name = SS.str();
    R->Write(Name);
  }
}
