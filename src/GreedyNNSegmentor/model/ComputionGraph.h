#ifndef SRC_ComputionGraph_H_
#define SRC_ComputionGraph_H_

#include "ModelParams.h"
#include "State.h"

struct COutput{
	PNode in;
	bool bGold;

	COutput() : in(NULL), bGold(0){
	}

	COutput(const COutput& other) : in(other.in), bGold(other.bGold){
	}
};

// Each model consists of two parts, building neural graph and defining output losses.
// This framework wastes memory
struct ComputionGraph : Graph{
	
public:	
	GlobalNodes globalNodes;
	// node instances
	CStateItem start;
	vector<CStateItem> states; 
	vector<vector<COutput> > outputs;

private:
	ModelParams *pModel;
	HyperParams *pOpts;

	// node pointers
public:
	ComputionGraph() : Graph(){
		clear();
	}

	~ComputionGraph(){
		clear();
	}

public:
	//allocate enough nodes 
	inline void initial(ModelParams& model, HyperParams& opts){
		std::cout << "state size: " << sizeof(CStateItem) << std::endl;
		std::cout << "action node size: " << sizeof(ActionedNodes) << std::endl;
		globalNodes.resize(max_sentence_clength);
		states.resize(opts.maxlength + 1);
		
		globalNodes.initial(model, opts);
		for (int idx = 0; idx < states.size(); idx++){
			states[idx].initial(model, opts);
		}
		start.clear();
		start.initial(model, opts);

		pModel = &model;
		pOpts = &opts;
	}

	inline void clear(){
		Graph::clear();
		states.clear();
		pModel = NULL;
		pOpts = NULL;
	}


public:
	// some nodes may behave different during training and decode, for example, dropout
	inline int forward(const std::vector<std::string>* pCharacters, const vector<CAction>* goldAC = NULL){
		//first step, clear node values
		if (goldAC != NULL){
			clearValue(true);  //train
		}
		else{
			clearValue(false); // decode
		}

		globalNodes.forward(this, pCharacters);
		//second step, build graph
		static CStateItem* pGenerator;
		static int step, offset;
		static std::vector<CAction> actions; // actions to apply for a candidate
		static CScoredState scored_action; // used rank actions
		static COutput output;
		static bool correct_action_scored;
		static bool correct_in_beam;
		static CAction answer, action;
		static vector<COutput> per_step_output;
		static NRHeap<CScoredState, CScoredState_Compare> beam;
		beam.resize(pOpts->action_num);

		start.setInput(pCharacters);
		pGenerator = &start;

		step = 0;
		while (true){			
			//prepare for the next
			pGenerator->prepare(pOpts, pModel, &globalNodes);
			
			answer.clear();
			per_step_output.clear();
			correct_action_scored = false;
			if (train) answer = (*goldAC)[step];
			beam.clear();

			pGenerator->getCandidateActions(actions);
			pGenerator->computeNextScore(this, actions);
			scored_action.item = pGenerator;
			for (int idy = 0; idy < actions.size(); ++idy) {
				scored_action.ac = actions[idy]._code;
				if (pGenerator->_bGold && actions[idy] == answer){
					scored_action.bGold = true; 
					correct_action_scored = true;
					output.bGold = true;
				}
				else{
					scored_action.bGold = false;
					output.bGold = false;
				}
				scored_action.score = pGenerator->_nextscores.outputs[scored_action.ac].val.coeff(0, 0);
				output.in = &(pGenerator->_nextscores.outputs[scored_action.ac]);
				beam.add_elem(scored_action);
				per_step_output.push_back(output);
			}
			
			outputs.push_back(per_step_output);

			if (train && !correct_action_scored){ //training
				std::cout << "error during training, gold-standard action is filtered" << std::endl;
			}

			offset = beam.elemsize();
			if (offset == 0){ // judge correctiveness
				std::cout << "error, reach no output here, please find why" << std::endl;
				for (int idx = 0; idx < pCharacters->size(); idx++) {
					std::cout << (*pCharacters)[idx] << std::endl;
				}
				std::cout << "" << std::endl;
				return -1;
			}

			beam.sort_elem();	

			if (train) {
				bool find_next = false;
				for (int idx = 0; idx < offset; idx++){
					if (beam[idx].bGold){
						pGenerator = beam[idx].item;
						action.set(beam[idx].ac);
						pGenerator->move(&(states[step]), action);
						states[step]._bGold = beam[idx].bGold;
						states[step]._score = &(pGenerator->_nextscores.outputs[beam[idx].ac]);
						find_next = true;
					}
				}
				if (!find_next){
					std::cout << "serious bug here" << std::endl;
					exit(0);
				}
			}
			else{
				pGenerator = beam[0].item;
				action.set(beam[0].ac);
				pGenerator->move(&(states[step]), action);
				states[step]._bGold = beam[0].bGold;
				states[step]._score = &(pGenerator->_nextscores.outputs[beam[0].ac]);
			}
		
			pGenerator = &(states[step]);
			if (states[step].IsTerminated()){
				break;
			}

			step++;
		}

		return step;
	}


	inline int extractFeat(const std::vector<std::string>* pCharacters, const vector<CAction>* goldAC){
		//first step, clear node values
		clearValue(true); // compute is a must step for train, predict and cost computation
		//second step, build graph
		static CStateItem* lastState;
		static CStateItem* pGenerator;
		static CAction answer;
		static int step;

		start.setInput(pCharacters);
		lastState = &start;

		step = 0;
		while (true){
			//prepare for the next
			lastState->prepare(pOpts, pModel, NULL);
			answer = (*goldAC)[step];
			pGenerator = &states[step];
			lastState->move(pGenerator, answer);
			pGenerator->collectFeat(pModel);
			if (pGenerator->IsTerminated()){
				break;
			}
			lastState = pGenerator;
			step++;
		}

		return 1;
	}

public:
	inline void clearValue(const bool& bTrain){
		Graph::clearValue(bTrain);
		clearVec(outputs);
	}

};

#endif /* SRC_ComputionGraph_H_ */