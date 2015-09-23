#ifndef geometryConstruction_hh
#define geometryConstruction_hh 1

#include "G4Box.hh"
#include "G4Tubs.hh"
#include "G4LogicalVolume.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VUserDetectorConstruction.hh"


class geometryConstruction : public G4VUserDetectorConstruction
{
  
public:
  geometryConstruction(); 
  ~geometryConstruction();
  
  G4VPhysicalVolume *Construct();
  void BuildScintillator();

private:
  // Member data for the world
  G4Box *World_S;
  G4LogicalVolume *World_L;
  G4VPhysicalVolume *World_P;
  G4double World_X, World_Y, World_Z;

  // Member data for the scintillator
  G4Box *BGO_S;
  G4Tubs *NaI_S;
  G4LogicalVolume *BGO_L, *NaI_L;
  G4VPhysicalVolume *BGO_P, *NaI_P;
  G4double BGO_X, BGO_Y, BGO_Z;
  G4double NaI_RMin, NaI_RMax, NaI_Z;
};

#endif
