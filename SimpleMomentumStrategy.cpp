/*================================================================================                               
*     Source: ../Lime/StrategyStudio/examples/strategies/SimpleMomentumStrategy/SimpleMomentumStrategy.cpp                                                        
*     Last Update: 2013/6/1 13:55:14                                                                            
*     Contents:                                     
*     Distribution:          
*                                                                                                                
*                                                                                                                
*     Copyright (c) Lime Brokerage LLC, 2011 - 2013.                                                  
*     All rights reserved.                                                                                       
*                                                                                                                
*     This software is part of Licensed material, which is the property of Lime Brokerage LLC ("Company"), 
*     and constitutes Confidential Information of the Company.                                                  
*     Unauthorized use, modification, duplication or distribution is strictly prohibited by Federal law.         
*     No title to or ownership of this software is hereby transferred.                                          
*                                                                                                                
*     The software is provided "as is", and in no event shall the Company or any of its affiliates or successors be liable for any 
*     damages, including any lost profits or other incidental or consequential damages relating to the use of this software.       
*     The Company makes no representations or warranties, express or implied, with regards to this software.      
*	  
*	  May 23, 2018
/*================================================================================*/   

#ifdef _WIN32
    #include "stdafx.h"
#endif

#include "SimpleMomentumStrategy.h"

#include "FillInfo.h"
#include "AllEventMsg.h"
#include "ExecutionTypes.h"
#include <Utilities/Cast.h>
#include <Utilities/utils.h>

#include <math.h>
#include <iostream>
#include <cassert>
#include <string>

using namespace LimeBrokerage::StrategyStudio;
using namespace LimeBrokerage::StrategyStudio::MarketModels;
using namespace LimeBrokerage::StrategyStudio::Utilities;

using namespace std;

SimpleMomentum::SimpleMomentum(StrategyID strategyID, const std::string& strategyName, const std::string& groupName) :
	Strategy(strategyID, strategyName, groupName),
	m_instrument_order_id_map(),
	m_position_size(1),
	m_debug_on(false),
	m_risk_reward_ratio(0), //no stop, otherwise enter the reward value. Calculated off a risk of 1
	m_percent_from_MA_target(1),
	m_MA_length(5), //number of closing bars to be used in moving average
	m_allow_long_trades(true), //defrault set to make both long and short trades
	m_allow_short_trades(true),
	m_static_moving_average(false), //default set to use a dynamic moving average
	m_new_bars_off_hours(false),
	m_moving_average_composition('a'), //a -ask ::: b -bid ::: m -mid
	m_length_of_bars(3600), //time in seconds
	m_max_percent_from_MA_target(0), //default is zero - off. 
	m_num_losing_trades_before_halt(0), //default is zero - off.
	m_flatten_at_eod(0)
{
    this->set_enabled_pre_open_data_flag(true);
    this->set_enabled_pre_open_trade_flag(true);
    this->set_enabled_post_close_data_flag(true);
    this->set_enabled_post_close_trade_flag(true);

	instrument = instrument_begin()->second;
	holding_contract = false;
	went_long = false;
	entry_price = 0;
	five_period_moving_average = 0;
	stop_price = 0;
	market_state = 0;
	num_consecutive_losing_trades = 0;
	halt_trading = false;
	last_order = "";
}

SimpleMomentum::~SimpleMomentum()
{
}

void SimpleMomentum::OnResetStrategyState()
{
	cout << "RESETTING CLIENT" << endl;
    m_instrument_order_id_map.clear();

	instrument = instrument_begin()->second;

	holding_contract = false;
	went_long = false;
	entry_price = 0;
	five_period_moving_average = 0;
	stop_price = 0;
	market_state = 0;
	moving_average_window.clear();
	num_consecutive_losing_trades = 0;
	halt_trading = false;
	last_order = "";
}

//adds clickable items to client gui
void SimpleMomentum::DefineStrategyParams()
{
    CreateStrategyParamArgs arg1("position_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, m_position_size);
    params().CreateParam(arg1);
    
    CreateStrategyParamArgs arg2("debug", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_debug_on);
    params().CreateParam(arg2);

	CreateStrategyParamArgs arg3("risk_reward_ratio", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, m_risk_reward_ratio);
	params().CreateParam(arg3);

	CreateStrategyParamArgs arg4("percent_from_MA_trigger", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, m_percent_from_MA_target);
	params().CreateParam(arg4);

	CreateStrategyParamArgs arg5("window_size", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, m_MA_length);
	params().CreateParam(arg5);

	CreateStrategyParamArgs arg6("allow_long_trades", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_allow_long_trades);
	params().CreateParam(arg6);

	CreateStrategyParamArgs arg7("allow_short_trades", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_allow_short_trades);
	params().CreateParam(arg7);

	CreateStrategyParamArgs arg8("static_moving_average", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_static_moving_average);
	params().CreateParam(arg8);

	CreateStrategyParamArgs arg9("new_bars_off_hours", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_new_bars_off_hours);
	params().CreateParam(arg9);

	CreateStrategyParamArgs arg10("MA_comp", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_CHAR, m_moving_average_composition);
	params().CreateParam(arg10);

	CreateStrategyParamArgs arg11("length_of_bars", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, m_length_of_bars);
	params().CreateParam(arg11);

	CreateStrategyParamArgs arg12("max_percentage_from_MA_trigger", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_DOUBLE, m_max_percent_from_MA_target);
	params().CreateParam(arg12);

	CreateStrategyParamArgs arg13("num_trades_before_halting", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_INT, m_num_losing_trades_before_halt);
	params().CreateParam(arg13);

	CreateStrategyParamArgs arg14("flatten_at_eod", STRATEGY_PARAM_TYPE_RUNTIME, VALUE_TYPE_BOOL, m_flatten_at_eod);
	params().CreateParam(arg14);
}

//adds clickable options to gui
void SimpleMomentum::DefineStrategyCommands()
{
	StrategyCommand command1(1, "Reset");
	commands().AddCommand(command1);
}

void SimpleMomentum::RegisterForStrategyEvents(StrategyEventRegister* eventRegister, DateType currDate)
{
    for (SymbolSetConstIter it = symbols_begin(); it != symbols_end(); ++it) {
        eventRegister->RegisterForBars(*it, BAR_TYPE_TIME, m_length_of_bars);
    }

	instrument = instrument_begin()->second;

	if (SymbolCount() != 1)
	{
		logger().LogToClient(LOGLEVEL_ERROR, "This strategy only trades one instrument");
		Stop();
		return;
	}
}

void SimpleMomentum::OnScheduledEvent(const ScheduledEventMsg& msg)
{	
	
}

void SimpleMomentum::OnTopQuote(const QuoteEventMsg& msg)
{
	//only execute this code during open hours
	if (state() == STRATEGY_STATE_RUNNING && (market_state == 2))
	{
		double current_ask_price = msg.quote().ask();
		double current_bid_price = msg.quote().bid();
		double current_mid_price = msg.quote().mid_price();

		//if the stategy already has a full window and the user wants to allow a dynamic moving average, then use the current ask price to update the fifth price in the moving average 
		if (moving_average_window.size() == m_MA_length)
		{
			if (!m_static_moving_average)
			{
				switch (m_moving_average_composition)
				{
				case 'a':
					moving_average_window[m_MA_length - 1] = current_ask_price;
					break;
				case 'b':
					moving_average_window[m_MA_length - 1] = current_bid_price;
					break;
				case 'm':
					moving_average_window[m_MA_length - 1] = current_mid_price;
					break;
				default:
					cout << "ERROR: moving_average_composition must be set to either 'a' (ask) 'b' (bid) or 'm' (mid)" << endl;
					return;
				}

			}
		}
		//otherwise the window does not have enough data to work with the strategy and should just wait until the strategy gets enough data
		else
		{
			//exit from within this function call.
			return;
		}

		//turn signal on to get rid of a held contract if holding within the last 5 minutes of the open hours
		bool lock_trading = false;

		//if the user enabled the option to flatten at end of day
		if (m_flatten_at_eod)
		{
			TimeType quote_time = ConvertUTCToUSCentral(msg.source_time());

			if (quote_time.time_of_day().hours() >= 13 && quote_time.time_of_day().minutes() >= 15 && quote_time.time_of_day().seconds() >= 0)
			{
				lock_trading = true;
			}
		}

		//force close a position before market close and exit the function
		if (lock_trading)
		{
			if (holding_contract && went_long)
			{
				OrderParams params(*instrument, m_position_size, current_bid_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_SELL, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

				if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
					m_instrument_order_id_map[instrument] = params.order_id;
					if (m_debug_on)
					{
						cout << "FORCED SOLD LONG AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
					}
					holding_contract = false;
					went_long = false;
				}
			}
			else if (holding_contract && !went_long)
			{
				OrderParams params(*instrument, m_position_size, current_ask_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_BUY, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

				if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
					m_instrument_order_id_map[instrument] = params.order_id;
					if (m_debug_on)
					{
						cout << "FORCED COVERED SHORT AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
					}
					holding_contract = false;
					went_long = false;
				}
			}
		}

		else
		{
			//if the portfolio is already managing a contract, then continue to monitor it every second
			//if (portfolio().Contains(instrument))
			if (holding_contract)
			{
				//holding a long position
				if (went_long)
				{
					//sell if price has met the moving average or jumped over it
					if (current_bid_price >= five_period_moving_average)
					{
						OrderParams params(*instrument, m_position_size, current_bid_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_SELL, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

						if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
							m_instrument_order_id_map[instrument] = params.order_id;
							if (m_debug_on)
							{
								cout << "SOLD LONG AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
							}
							holding_contract = false;
							went_long = false;
						}
					}

					//sell at stop price
					else if (current_bid_price <= stop_price)
					{
						if (stop_price != 0)
						{
							//cout << "current_bid_price: " << current_bid_price << " ::: stop_price: " << stop_price << endl;
							OrderParams params(*instrument, m_position_size, current_bid_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_SELL, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

							if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
								m_instrument_order_id_map[instrument] = params.order_id;
								if (m_debug_on)
								{
									cout << "TRIGGERED STOP - SOLD LONG AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
								}
								holding_contract = false;
								went_long = false;
								num_consecutive_losing_trades += 1;
							}
						}
					}
				}

				//holding a short position
				else
				{
					//cover short if price has met the moving average or jumped below it
					if (current_ask_price <= five_period_moving_average)
					{
						//cout << "current_ask_price: " << current_ask_price << " ::: five_period_moving_average: " << five_period_moving_average << endl;
						OrderParams params(*instrument, m_position_size, current_ask_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_BUY, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

						if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
							m_instrument_order_id_map[instrument] = params.order_id;
							if (m_debug_on)
							{
								cout << "COVERED SHORT AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
							}
							holding_contract = false;
							went_long = false;
						}
					}
					//cover short at stop price
					else if (current_ask_price >= stop_price)
					{
						if (stop_price != 0)
						{
							//cout << "current_ask_price: " << current_ask_price << " ::: stop_price: " << stop_price << endl;
							OrderParams params(*instrument, m_position_size, current_ask_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_BUY, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

							if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
								m_instrument_order_id_map[instrument] = params.order_id;
								if (m_debug_on)
								{
									cout << "TRIGGERED STOP - COVERED SHORT AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
								}
								holding_contract = false;
								went_long = false;
								num_consecutive_losing_trades += 1;
							}
						}
					}
				}
			}

			//otherwise if the portfolio is empty, check if now is a good time to make a trade
			else
			{
				//
				//calculate the + or - targets to help determine if the current price is within the current threshold to buy or sell
				double price_deviation = m_percent_from_MA_target / 100;
				double short_target_price = five_period_moving_average *  (1 + price_deviation);
				double long_target_price = five_period_moving_average * (1 - price_deviation);

				double max_price_deviation_percent = m_max_percent_from_MA_target / 100;
				double max_price_deviation_short = five_period_moving_average * (1 + max_price_deviation_percent);
				double max_price_deviation_long = five_period_moving_average * (1 - max_price_deviation_percent);

				//go long - make sure longs are allowable and there is enough money
				if ((current_ask_price <= long_target_price) && m_allow_long_trades)
				{
					//check if current price is beyond price deviation
					if ((m_max_percent_from_MA_target != 0 && current_ask_price >= max_price_deviation_long) || m_max_percent_from_MA_target == 0)
					{
						//check if the user didn't want to halt trading after losses or if their threshold hasn't been met yet
						if (m_num_losing_trades_before_halt == 0 || num_consecutive_losing_trades < m_num_losing_trades_before_halt)
						{
							OrderParams params(*instrument, m_position_size, current_ask_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_BUY, ORDER_TIF_GTX, ORDER_TYPE_MARKET);
							if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
								m_instrument_order_id_map[instrument] = params.order_id;
								if (m_debug_on)
								{
									cout << "WENT LONG AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
									cout << "current moving average: " << five_period_moving_average << endl;
								}
								holding_contract = true;
								entry_price = params.price;
								went_long = true;
								last_order = "LONG";
								if (m_risk_reward_ratio == 0)
								{
									set_stop(entry_price, " ");
								}
								else
								{
									set_stop(entry_price, "LONG");
								}
							}
						}
						//if the threshold was met, then check if price action triggered a short trade last, making the strategy able to trade again
						else if (num_consecutive_losing_trades >= m_num_losing_trades_before_halt)
						{
							//if the last order was not an opposite trade, then an order will not go through to make another long trade
							if (last_order == "SHORT")
							{
								OrderParams params(*instrument, m_position_size, current_ask_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_BUY, ORDER_TIF_GTX, ORDER_TYPE_MARKET);
								if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
									m_instrument_order_id_map[instrument] = params.order_id;
									if (m_debug_on)
									{
										cout << "WENT LONG AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
										cout << "current moving average: " << five_period_moving_average << endl;
									}
									holding_contract = true;
									entry_price = params.price;
									went_long = true;
									last_order = "LONG";
									num_consecutive_losing_trades = 0;
									if (m_risk_reward_ratio == 0)
									{
										set_stop(entry_price, " ");
									}
									else
									{
										set_stop(entry_price, "LONG");
									}
								}
							}
							else
							{
								if (m_debug_on)
								{
									cout << "halted" << endl;
								}
							}
						}
					}
				}
				//go short - make sure shorts are allowable and there is enough money
				else if ((current_bid_price >= short_target_price) && m_allow_short_trades)
				{
					//check if current price is beyond price deviation
					if ((m_max_percent_from_MA_target != 0 && current_bid_price <= max_price_deviation_short) || m_max_percent_from_MA_target == 0)
					{
						//check if the user didn't want to halt trading after losses or if their threshold hasn't been met yet
						if (m_num_losing_trades_before_halt == 0 || num_consecutive_losing_trades < m_num_losing_trades_before_halt)
						{
							OrderParams params(*instrument, m_position_size, current_bid_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_SELL, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

							if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
								m_instrument_order_id_map[instrument] = params.order_id;
								if (m_debug_on)
								{
									cout << "WENT SHORT AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
									cout << "current moving average: " << five_period_moving_average << endl;
								}
								holding_contract = true;
								entry_price = params.price;
								went_long = false;
								last_order = "SHORT";
								if (m_risk_reward_ratio == 0)
								{
									set_stop(entry_price, " ");
								}
								else
								{
									set_stop(entry_price, "SHORT");
								}
							}
						}
						else if (num_consecutive_losing_trades >= m_num_losing_trades_before_halt)
						{
							//if the last order was not an opposite trade, then an order will not go through to make another short trade
							if (last_order == "LONG")
							{
								OrderParams params(*instrument, m_position_size, current_bid_price, MARKET_CENTER_ID_CME_GLOBEX, ORDER_SIDE_SELL, ORDER_TIF_GTX, ORDER_TYPE_MARKET);

								if (trade_actions()->SendNewOrder(params) == TRADE_ACTION_RESULT_SUCCESSFUL) {
									m_instrument_order_id_map[instrument] = params.order_id;
									if (m_debug_on)
									{
										cout << "WENT SHORT AT: " << params.price << " with " << params.quantity << " contract(s)" << endl;
										cout << "current moving average: " << five_period_moving_average << endl;
									}
									holding_contract = true;
									entry_price = params.price;
									went_long = false;
									last_order = "SHORT";
									if (m_risk_reward_ratio == 0)
									{
										set_stop(entry_price, " ");
									}
									else
									{
										set_stop(entry_price, "SHORT");
									}
								}
							}
							else
							{
								if (m_debug_on)
								{
									cout << "HALTED" << endl;
								}
							}
						}
					}
				}
			}
		}

		//calculate the new moving average after strategy choices were reviewed with the last second's moving average
		double sum = 0;
		for (int i = 0; i < moving_average_window.size(); i++)
		{
			sum += moving_average_window[i];
		}
		five_period_moving_average = sum / moving_average_window.size();
	}
}

void SimpleMomentum::OnBar(const BarEventMsg& msg)
{
	//execute the code if the user wants to run it during off hours or by default during open hours
	if (m_new_bars_off_hours || market_state == 2)
	{
		if (m_debug_on) {
			ostringstream str;
			str << msg.instrument().symbol() << ": " << msg.bar();
			logger().LogToClient(LOGLEVEL_DEBUG, str.str().c_str());
		}

		TimeType closing_bar = msg.bar_time();

		if (msg.bar().close() == 0)
		{
			logger().LogToClient(LOGLEVEL_DEBUG, "COULD NOT RETRIEVE CLOSE PRICE");
			//do nothing for now
		}
		else if (moving_average_window.size() != m_MA_length)
		{
			moving_average_window.push_back(msg.bar().close());

			if (m_debug_on)
			{
				cout << "pushing back a bar close price into the moving average window: " << msg.bar().close() << endl;
			}

			logger().LogToClient(LOGLEVEL_DEBUG, boost::to_string(msg.bar().close()));

			//calculate an incomplete moving average so by the time the fourth bar has closed and trading opens up, the average will have been initialized safetly
			double sum = 0;
			for (int i = 0; i < moving_average_window.size(); i++)
			{
				sum += moving_average_window[i];
			}
			five_period_moving_average = sum / moving_average_window.size();

			//push back another element to allow for the active bar price to start being stored
			if (!m_static_moving_average)
			{
				if (moving_average_window.size() == m_MA_length - 1)
				{
					moving_average_window.push_back(0);
				}
			}
			
			logger().LogToClient(LOGLEVEL_DEBUG, boost::to_string(five_period_moving_average));
		}
		else
		{
			//remove the first element in the window and shift everything down one
			moving_average_window.erase(moving_average_window.begin());
			if (m_MA_length < 2)
			{
				cout << "ERROR: 'window_size' must be an integer greater than or equal to 2" << endl;
				return;
			}
			else
			{
				//reset the second to last bar price with the new closing bar price
				moving_average_window.insert(moving_average_window.begin() + m_MA_length - 1, msg.bar().close());
				//moving_average_window[window_size - 2] = msg.bar().close();
				if (m_debug_on)
				{
					cout << "removed least recent bar close and replaced it with the newest" << endl;
				}
			}
		}

		if (msg.bar().close() < .01) return;
	}
}

//called by sim at runtime
void SimpleMomentum::OnOrderUpdate(const OrderUpdateEventMsg& msg)  
{    
    if(msg.completes_order())
		m_instrument_order_id_map[msg.order().instrument()] = 0;
}

void SimpleMomentum::OnMarketState(const MarketStateEventMsg& msg) {
	logger().LogToClient(LOGLEVEL_DEBUG, boost::to_string(msg.market_state()));
	market_state = msg.market_state();
};

//adds logic to added clickable options 
void SimpleMomentum::OnStrategyCommand(const StrategyCommandEventMsg& msg)
{
    switch (msg.command_id()) {
		case 1:
			OnResetStrategyState();
        default:
            logger().LogToClient(LOGLEVEL_DEBUG, "Unknown strategy command received");
            break;
    }
}

//adds logic to changed list of parameters in client gui
void SimpleMomentum::OnParamChanged(StrategyParam& param)
{    
	if (param.param_name() == "position_size") {
        if (!param.Get(&m_position_size))
            throw StrategyStudioException("Could not get position size");
    } else if (param.param_name() == "debug") {
        if (!param.Get(&m_debug_on))
            throw StrategyStudioException("Could not get trade size");
	}else if (param.param_name() == "risk_reward_ratio") {
		if (!param.Get(&m_risk_reward_ratio))
			throw StrategyStudioException("Could not get risk reward ratio");
	}else if (param.param_name() == "percent_from_MA_trigger") {
		if (!param.Get(&m_percent_from_MA_target))
			throw StrategyStudioException("Could not get percent_from_MA_trigger");
	}else if (param.param_name() == "window_size") {
		if (!param.Get(&m_MA_length))
			throw StrategyStudioException("Could not get window_size");
	}else if (param.param_name() == "allow_long_trades") {
		if (!param.Get(&m_allow_long_trades))
			throw StrategyStudioException("Could not get allow_long_trades");
	}else if (param.param_name() == "allow_short_trades") {
		if (!param.Get(&m_allow_short_trades))
			throw StrategyStudioException("Could not get allow_short_trades");
	}else if (param.param_name() == "static_moving_average") {
		if (!param.Get(&m_static_moving_average))
			throw StrategyStudioException("Could not get static_moving_average");
	}else if (param.param_name() == "new_bars_off_hours") {
		if (!param.Get(&m_new_bars_off_hours))
			throw StrategyStudioException("Could not get new_bars_off_hours");
	}else if (param.param_name() == "MA_comp") {
		if (!param.Get(&m_moving_average_composition))
			throw StrategyStudioException("Could not get moving_average_composition");
	}else if (param.param_name() == "length_of_bars") {
		if (!param.Get(&m_length_of_bars))
			throw StrategyStudioException("Could not get length_of_bars");
	}else if (param.param_name() == "max_percentage_from_MA_trigger") {
		if (!param.Get(&m_max_percent_from_MA_target))
			throw StrategyStudioException("Could not get max_percentage_from_MA_Trigger");
	}else if (param.param_name() == "num_trades_before_halting") {
		if (!param.Get(&m_num_losing_trades_before_halt))
			throw StrategyStudioException("Could not get num_losing_trades_before_halt");
	}else if (param.param_name() == "flatten_at_eod") {
		if (!param.Get(&m_flatten_at_eod))
			throw StrategyStudioException("Could not get flatten_at_eod");
	}
}

void SimpleMomentum::OnTrade(const TradeDataEventMsg &msg)
{
	logger().LogToClient(LOGLEVEL_INFO, "TRADE OCCURED " + boost::to_string(msg.trade().size()));
}

void SimpleMomentum::set_stop(double entry_price, string trade_type)
{
	if (trade_type == "LONG")
	{
		double profit = five_period_moving_average - entry_price;
		stop_price = entry_price - (profit / m_risk_reward_ratio);
		if (m_debug_on)
		{
			cout << "setting stop at: " << stop_price << endl;
		}
	}
	else if (trade_type == "SHORT")
	{
		double profit = entry_price - five_period_moving_average;
		stop_price = entry_price + (profit / m_risk_reward_ratio);
		if (m_debug_on)
		{
			cout << "setting stop at: " << stop_price << endl;
		}
	}
	else
	{
		stop_price = 0;
		if (m_debug_on)
		{
			cout << "did not set stop" << endl;
		}
	}
}