#include "NetworkTrace.h"

#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGPowerCircuit.h"

#include "Network/FINNetworkConnector.h"
#include "Network/FINNetworkCircuit.h"
#include "util/Logging.h"

#define StepFuncName(A, B) Step ## _ ## A ## _ ## B
#define StepRegName(A, B) StepReg ## _ ## A ## _ ## B
#define StepRegSigName(A, B) StepRegSig ## _ ## A ## _ ## B
#define StepFuncSig(A, B) bool StepFuncName(A, B)(UObject* oA, UObject* oB)
#define StepFuncSigReg(A, B) \
	std::pair<std::pair<UClass*, UClass*>, std::pair<std::string, TraceStep*>> StepRegSigName(A, B)() { \
		return {{A::StaticClass(), B::StaticClass()}, {#A "_" #B, new TraceStep(&StepFuncName(A, B))}}; \
	}
#define Step(CA, CB) \
	StepFuncSig(CA, CB); \
	StepFuncSigReg(CA, CB) \
	TraceStepRegisterer StepRegName(CA, CB)(&StepRegSigName(CA, CB)); \
	StepFuncSig(CA, CB) { \
		CA* A = Cast<CA>(oA); \
		CB* B = Cast<CB>(oB);

namespace FicsItKernel {
	namespace Network {
		std::shared_ptr<TraceStep> NetworkTrace::fallbackTraceStep;
		std::vector<std::pair<std::pair<UClass*, UClass*>, std::pair<std::string, TraceStep*>>(*)()> NetworkTrace::toRegister;
		std::map<std::string, std::shared_ptr<TraceStep>> NetworkTrace::traceStepRegistry;
		std::map<std::shared_ptr<TraceStep>, std::string> NetworkTrace::inverseTraceStepRegistry;
		std::map<UClass*, std::pair<std::map<UClass*, std::shared_ptr<TraceStep>>, std::map<UClass*, std::shared_ptr<TraceStep>>>> NetworkTrace::traceStepMap;
		std::map<UClass*, std::pair<std::map<UClass*, std::shared_ptr<TraceStep>>, std::map<UClass*, std::shared_ptr<TraceStep>>>> NetworkTrace::interfaceTraceStepMap;
		
		class TraceStepRegisterer {
		public:
			TraceStepRegisterer(std::pair<std::pair<UClass*, UClass*>, std::pair<std::string, TraceStep*>>(*regSig)()) {
				NetworkTrace::toRegister.push_back(regSig);
			}
		};

		void traceRegisterSteps() {
			for (auto& stepSig : NetworkTrace::toRegister) {
				auto step = stepSig();
				TraceStep* tStep = step.second.second;
				UClass* A = step.first.first;
				UClass* B = step.first.second;
				std::map<UClass*, std::pair<std::map<UClass*, std::shared_ptr<TraceStep>>, std::map<UClass*, std::shared_ptr<TraceStep>>>>* AMap;
				if (A->GetSuperClass() == UInterface::StaticClass()) AMap = &NetworkTrace::interfaceTraceStepMap;
				else AMap = &NetworkTrace::traceStepMap;
				std::map<UClass*, std::shared_ptr<TraceStep>>* BMap;
				if (B->GetSuperClass() == UInterface::StaticClass()) BMap = &(*AMap)[A].second;
				else BMap = &(*AMap)[A].first;
				std::shared_ptr<TraceStep> stepPtr = std::shared_ptr<TraceStep>(tStep);
				(*BMap)[B] = stepPtr;
				NetworkTrace::traceStepRegistry[step.second.first] = stepPtr;
				NetworkTrace::inverseTraceStepRegistry[stepPtr] = step.second.first;
			}
			NetworkTrace::toRegister.clear();
		};
		
		std::shared_ptr<TraceStep> findTraceStep2(std::pair<std::map<UClass*, std::shared_ptr<TraceStep>>, std::map<UClass*, std::shared_ptr<TraceStep>>>& stepList, UClass* B) {
			UClass* Bi = B;
			while (Bi && Bi != UObject::StaticClass()) {
				auto stepB = stepList.first.find(Bi);
				if (stepB != stepList.first.end()) {
					return stepB->second;
				}
				Bi = Bi->GetSuperClass();
			}

			for (FImplementedInterface& interface : B->Interfaces) {
				auto stepB = stepList.second.find(interface.Class);
				if (stepB != stepList.second.end()) {
					return stepB->second;
				}
			}

			return nullptr;
		}

		std::shared_ptr<TraceStep> NetworkTrace::findTraceStep(UClass* A, UClass* B) {
			UClass* Ai = A;
			while (Ai && Ai != UObject::StaticClass()) {
				auto stepA = traceStepMap.find(Ai);
				if (stepA != traceStepMap.end()) {
					auto& stepAList = stepA->second;
					std::shared_ptr<TraceStep> step = findTraceStep2(stepAList, B);
					if (step.get()) return step;
				}
				Ai = Ai->GetSuperClass();
			}
			
			for (FImplementedInterface& interface : A->Interfaces) {
				auto stepA = interfaceTraceStepMap.find(interface.Class);
				if (stepA != interfaceTraceStepMap.end()) {
					auto& stepAList = stepA->second;
					std::shared_ptr<TraceStep> step = findTraceStep2(stepAList, B);
					if (step.get()) return step;
				}
			}

			return fallbackTraceStep;
		}

		NetworkTrace::NetworkTrace(const NetworkTrace& trace) {
			traceRegisterSteps();
			
			prev = (trace.prev) ? new NetworkTrace(*trace.prev) : nullptr;
			step = trace.step;
			obj = trace.obj;
		}

		NetworkTrace::NetworkTrace(NetworkTrace&& trace) {
			traceRegisterSteps();
			
			prev = trace.prev;
			step = trace.step;
			obj = trace.obj;
			trace.prev = nullptr;
		}

		NetworkTrace& NetworkTrace::operator=(const NetworkTrace& trace) {
			prev = (trace.prev) ? new NetworkTrace(*trace.prev) : nullptr;
			step = trace.step;
			obj = trace.obj;

			return *this;
		}

		NetworkTrace& NetworkTrace::operator=(NetworkTrace&& trace) {
			prev = trace.prev;
			step = trace.step;
			obj = trace.obj;
			trace.prev = nullptr;

			return *this;
		}

		NetworkTrace::NetworkTrace() : NetworkTrace(nullptr) {
			traceRegisterSteps();
		}

		NetworkTrace::NetworkTrace(UObject* obj) : obj(obj) {
			traceRegisterSteps();
		}

		NetworkTrace::~NetworkTrace() {
			if (prev) delete prev;
			prev = nullptr;
		}
		
		NetworkTrace NetworkTrace::operator/(UObject* other) const {
			NetworkTrace trace(other);

			UObject* A = obj.Get();
			if (!IsValid(A) || !IsValid(other)) return NetworkTrace(nullptr); // if A is not valid, the network trace will always be not invalid
			trace.prev = new NetworkTrace(*this);
			trace.step = findTraceStep(A->GetClass(), other->GetClass());
			return trace;
		}

		NetworkTrace NetworkTrace::operator/(std::pair<UObject*, std::shared_ptr<TraceStep>> other) {
			NetworkTrace trace(other.first);
			trace.prev = new NetworkTrace(*this);
			trace.step = other.second;
			return trace;
		}

		UObject* NetworkTrace::operator*() const {
			UObject* B = obj.Get();
			if (isValid() && IsValid(B)) {
				return B;
			} else {
				return nullptr;
			}
		}

		UObject* NetworkTrace::operator->() const {
			return **this;
		}

		NetworkTrace NetworkTrace::operator()(UObject* other) const {
			if (!IsValid(other)) return NetworkTrace(nullptr);

			NetworkTrace trace(*this);
			trace.obj = other;
			
			if (trace.prev) {
				auto A = trace.prev->obj.Get();
				if (!IsValid(A)) return NetworkTrace(nullptr); // if the previous network trace object is invalid, the trace will be always invalid
				trace.step = findTraceStep(trace.prev->obj->GetClass(), other->GetClass());
			}

			return trace;
		}

		bool NetworkTrace::operator==(const NetworkTrace& other) const {
			return obj == other.obj;
		}

		void NetworkTrace::checkTrace() const {
			if (!**this) throw std::exception("Object is not reachable");
		}

		NetworkTrace NetworkTrace::reverse() const {
			if (!obj.IsValid()) return NetworkTrace(nullptr);
			NetworkTrace trace(obj.Get());
			NetworkTrace* prev = this->prev;
			while (prev) {
				trace = trace / prev->obj.Get();
				prev = prev->prev;
			}
			return trace;
		}

		bool NetworkTrace::isValid() const {
			UObject* A = obj.Get();
			if (!IsValid(A)) return false;
			if (prev && step) {
				UObject* B = prev->obj.Get();
				if (!IsValid(B) || !(*step)(A, B)) return false;
			}
			if (prev) {
				return prev->isValid();
			}
			return true;
		}

		bool NetworkTrace::isEqualObj(const NetworkTrace& other) const {
			return obj == other.obj;
		}

		bool NetworkTrace::operator<(const NetworkTrace& other) const {
			struct TWOP {
				int32		ObjectIndex;
				int32		ObjectSerialNumber;
			};

			TWOP* d1 = (TWOP*)&obj;
			TWOP* d2 = (TWOP*)&other.obj;
			if (d1->ObjectIndex < d2->ObjectIndex) return true;
			else return d1->ObjectSerialNumber < d2->ObjectSerialNumber;
		}

		TWeakObjectPtr<UObject> NetworkTrace::getUnderlyingPtr() const {
			return obj;
		}

		/* ############### */
		/* # Trace Steps # */
		/* ############### */
		
		Step(UFGPowerConnectionComponent, UFGPowerCircuit)
			return A->GetCircuitID() == B->GetCircuitID();
		}
		Step(UFGPowerCircuit, UFGPowerConnectionComponent)
			return A->GetCircuitID() == B->GetCircuitID();
		}

		Step(UFGPowerInfoComponent, UFGPowerCircuit)
			return A->GetPowerCircuit()->GetCircuitID() == B->GetCircuitID();
		}
		Step(UFGPowerCircuit, UFGPowerInfoComponent)
			return A->GetCircuitID() == B->GetPowerCircuit()->GetCircuitID();
		}

		Step(UFGPowerInfoComponent, UFGPowerConnectionComponent)
			return A == B->GetPowerInfo();
		}
		Step(UFGPowerConnectionComponent, UFGPowerInfoComponent)
			return A->GetPowerInfo() == B;
		}

		Step(UFINNetworkComponent, UFINNetworkComponent)
			return IFINNetworkComponent::Execute_GetCircuit(oA)->HasNode(oB);
		}

		Step(UFINNetworkComponent, UFINNetworkCircuit)
			return B->HasNode(oA);
		}
		Step(UFINNetworkCircuit, UFINNetworkComponent)
			return A->HasNode(oB);
		}
	}
}