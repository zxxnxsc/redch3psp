#include "common.h"

#include "main.h"
#include "FileMgr.h"
#include "Physical.h"
#include "HandlingMgr.h"

cHandlingDataMgr mod_HandlingManager;

const char *HandlingFilename = "HANDLING.CFG";

const char VehicleNames[NUMHANDLINGS][14] = {
	"LANDSTAL",
	"IDAHO",
	"STINGER",
	"LINERUN",
	"PEREN",
	"SENTINEL",
	"PATRIOT",
	"FIRETRUK",
	"TRASH",
	"STRETCH",
	"MANANA",
	"INFERNUS",
	"BLISTA",
	"PONY",
	"MULE",
	"CHEETAH",
	"AMBULAN",
	"FBICAR",
	"MOONBEAM",
	"ESPERANT",
	"TAXI",
	"KURUMA",
	"BOBCAT",
	"MRWHOOP",
	"BFINJECT",
	"POLICE",
	"ENFORCER",
	"SECURICA",
	"BANSHEE",
	"PREDATOR",
	"BUS",
	"RHINO",
	"BARRACKS",
	"TRAIN",
	"HELI",
	"DODO",
	"COACH",
	"CABBIE",
	"STALLION",
	"RUMPO",
	"RCBANDIT",
	"BELLYUP",
	"MRWONGS",
	"MAFIA",
	"YARDIE",
	"YAKUZA",
	"DIABLOS",
	"COLUMB",
	"HOODS",
	"AIRTRAIN",
	"DEADDODO",
	"SPEEDER",
	"REEFER",
	"PANLANT",
	"FLATBED",
	"YANKEE",
	"BORGNINE"
};

cHandlingDataMgr::cHandlingDataMgr(void)
{
	memset(this, 0, sizeof(*this));
}

void
cHandlingDataMgr::Initialise(void)
{
	LoadHandlingData();
	field_0 = 0.1f;
	fWheelFriction = 0.9f;
	field_8 = 1.0f;
	field_C = 0.8f;
	field_10 = 0.98f;
}

void
cHandlingDataMgr::LoadHandlingData(void)
{
	char line[201];
	bool loaded[NUMHANDLINGS];
	memset(loaded, 0, sizeof(loaded));

	// Safe raw defaults keep the world bootable even if one optional/custom
	// handling row is absent. Valid HANDLING.CFG rows overwrite all of these.
	for(int i = 0; i < NUMHANDLINGS; i++) {
		tHandlingData *h = &HandlingData[i];
		memset(h, 0, sizeof(*h));
		h->nIdentifier = (tVehicleType)i;
		h->fMass = 1000.0f;
		h->Dimension = CVector(2.0f, 4.0f, 1.5f);
		h->nPercentSubmerged = 80;
		h->fTractionMultiplier = 1.0f;
		h->fTractionLoss = 0.8f;
		h->fTractionBias = 0.5f;
		h->Transmission.nNumberOfGears = 5;
		h->Transmission.fMaxVelocity = 140.0f;
		h->Transmission.fEngineAcceleration = 8.0f * 0.4f;
		h->Transmission.nDriveType = 'R';
		h->Transmission.nEngineType = 'P';
		h->fBrakeDeceleration = 8.0f;
		h->fBrakeBias = 0.5f;
		h->fSteeringLock = 30.0f;
		h->fSuspensionForceLevel = 1.0f;
		h->fSuspensionDampingLevel = 0.1f;
		h->fSuspensionUpperLimit = 0.3f;
		h->fSuspensionLowerLimit = -0.15f;
		h->fSuspensionBias = 0.5f;
		h->fCollisionDamageMultiplier = 1.0f;
		h->nMonetaryValue = 10000;
	}

	CFileMgr::SetDir("DATA");
	ssize_t length = CFileMgr::LoadFile(HandlingFilename, work_buff,
	                                     sizeof(work_buff)-1, "r");
	CFileMgr::SetDir("");
	if(length < 0) length = 0;
	work_buff[length] = '\0';

	char *cursor = (char*)work_buff;
	char *limit = cursor + length;
	while(cursor < limit) {
		char *end = cursor;
		while(end < limit && *end != '\n') end++;
		size_t lineLength = end - cursor;
		if(lineLength > sizeof(line)-1) lineLength = sizeof(line)-1;
		memcpy(line, cursor, lineLength);
		line[lineLength] = '\0';
		cursor = end < limit ? end+1 : limit;

		while(lineLength && (line[lineLength-1] == '\r' ||
		       line[lineLength-1] == ' ' || line[lineLength-1] == '\t'))
			line[--lineLength] = '\0';
		char *text = line;
		while(*text == ' ' || *text == '\t' || *text == '\r') text++;
		if(*text == '\0' || *text == ';') {
			if(strcmp(text, ";the end") == 0) break;
			continue;
		}

		char *word = strtok(text, " \t");
		if(word == nil) continue;
		int handlingId = FindExactWord(word, (const char*)VehicleNames, 14, NUMHANDLINGS);
		if(handlingId < 0 || handlingId >= NUMHANDLINGS) continue;
		tHandlingData *handling = &HandlingData[handlingId];
		handling->nIdentifier = (tVehicleType)handlingId;
		int field = 1;
		for(word = strtok(nil, " \t"); word; word = strtok(nil, " \t"), field++) {
			switch(field) {
			case  1: handling->fMass = strtod(word, nil); break;
			case  2: handling->Dimension.x = strtod(word, nil); break;
			case  3: handling->Dimension.y = strtod(word, nil); break;
			case  4: handling->Dimension.z = strtod(word, nil); break;
			case  5: handling->CentreOfMass.x = strtod(word, nil); break;
			case  6: handling->CentreOfMass.y = strtod(word, nil); break;
			case  7: handling->CentreOfMass.z = strtod(word, nil); break;
			case  8: handling->nPercentSubmerged = atoi(word); break;
			case  9: handling->fTractionMultiplier = strtod(word, nil); break;
			case 10: handling->fTractionLoss = strtod(word, nil); break;
			case 11: handling->fTractionBias = strtod(word, nil); break;
			case 12: handling->Transmission.nNumberOfGears = atoi(word); break;
			case 13: handling->Transmission.fMaxVelocity = strtod(word, nil); break;
			case 14: handling->Transmission.fEngineAcceleration = strtod(word, nil) * 0.4f; break;
			case 15: handling->Transmission.nDriveType = word[0]; break;
			case 16: handling->Transmission.nEngineType = word[0]; break;
			case 17: handling->fBrakeDeceleration = strtod(word, nil); break;
			case 18: handling->fBrakeBias = strtod(word, nil); break;
			case 19: handling->bABS = !!atoi(word); break;
			case 20: handling->fSteeringLock = strtod(word, nil); break;
			case 21: handling->fSuspensionForceLevel = strtod(word, nil); break;
			case 22: handling->fSuspensionDampingLevel = strtod(word, nil); break;
			case 23: handling->fSeatOffsetDistance = strtod(word, nil); break;
			case 24: handling->fCollisionDamageMultiplier = strtod(word, nil); break;
			case 25: handling->nMonetaryValue = atoi(word); break;
			case 26: handling->fSuspensionUpperLimit = strtod(word, nil); break;
			case 27: handling->fSuspensionLowerLimit = strtod(word, nil); break;
			case 28: handling->fSuspensionBias = strtod(word, nil); break;
			case 29:
				sscanf(word, "%x", &handling->Flags);
				handling->Transmission.Flags = handling->Flags;
				break;
			case 30: handling->FrontLights = atoi(word); break;
			case 31: handling->RearLights = atoi(word); break;
			}
		}
		loaded[handlingId] = field >= 30;
	}

	for(int i = 0; i < NUMHANDLINGS; i++)
		ConvertDataToGameUnits(&HandlingData[i]);
}

int
cHandlingDataMgr::FindExactWord(const char *word, const char *words, int wordLen, int numWords)
{
	int i;

	for(i = 0; i < numWords; i++){
		// BUG: the game does something really stupid here, it's fixed here
		if(strncmp(word, words, wordLen) == 0)
			return i;
		words += wordLen;
	}
	return numWords;
}


void
cHandlingDataMgr::ConvertDataToGameUnits(tHandlingData *handling)
{
	if(handling == nil)
		return;
	// acceleration is in ms^-2, but we need mf^-2 where f is one frame time (50fps)
	float velocity, a, b;

	handling->Transmission.fEngineAcceleration *= 1.0f/(50.0f*50.0f);
	handling->Transmission.fMaxVelocity *= 1000.0f/(60.0f*60.0f * 50.0f);
	handling->fBrakeDeceleration *= 1.0f/(50.0f*50.0f);
	handling->fTurnMass = (sq(handling->Dimension.x) + sq(handling->Dimension.y)) * handling->fMass / 12.0f;
	if(handling->fTurnMass < 10.0f)
		handling->fTurnMass *= 5.0f;
	handling->fInvMass = 1.0f/handling->fMass;
	handling->fBuoyancy = 100.0f/handling->nPercentSubmerged * GRAVITY*handling->fMass;

	// Don't quite understand this. What seems to be going on is that
	// we calculate a drag (air resistance) deceleration for a given velocity and
	// find the intersection between that and the max engine acceleration.
	// at that point the car cannot accelerate any further and we've found the max velocity.
	a = 0.0f;
	b = 100.0f;
	velocity = handling->Transmission.fMaxVelocity;
	while(a < b && velocity > 0.0f){
		velocity -= 0.01f;
		// what's the 1/6?
		a = handling->Transmission.fEngineAcceleration/6.0f;
		// no density or drag coefficient here...
		float a_drag = 0.5f*SQR(velocity) * handling->Dimension.x*handling->Dimension.z / handling->fMass;
		// can't make sense of this... maybe  v - v/(drag + 1)  ? but that doesn't make so much sense either
		b = -velocity * (1.0f/(a_drag + 1.0f) - 1.0f);
	}

	if(handling->nIdentifier == HANDLING_RCBANDIT){
		handling->Transmission.fMaxCruiseVelocity = handling->Transmission.fMaxVelocity;
	}else{
		handling->Transmission.fMaxCruiseVelocity = velocity;
		handling->Transmission.fMaxVelocity = velocity * 1.2f;
	}
	handling->Transmission.fMaxReverseVelocity = -0.2f;

	if(handling->Transmission.nDriveType == '4')
		handling->Transmission.fEngineAcceleration /= 4.0f;
	else
		handling->Transmission.fEngineAcceleration /= 2.0f;

	handling->Transmission.InitGearRatios();
}

int32
cHandlingDataMgr::GetHandlingId(const char *name)
{
	int i;
	for(i = 0; i < NUMHANDLINGS; i++)
		if(strncmp(VehicleNames[i], name, 14) == 0)
			break;
	return i;
}

void
cHandlingDataMgr::ConvertDataToWorldUnits(tHandlingData *handling)
{
	// TODO: mobile code
}

void
cHandlingDataMgr::RangeCheck(tHandlingData *handling)
{
	// TODO: mobile code
}

void
cHandlingDataMgr::ModifyHandlingValue(CVehicle *, const tVehicleType &, const tField &, const bool &)
{
	// TODO: mobile code
}

void
cHandlingDataMgr::DisplayHandlingData(CVehicle *, tHandlingData *, uint8, bool)
{
	// TODO: mobile code
}
