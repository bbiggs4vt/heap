///
/// Test bench comparing SortedHeap.h and SortedHeapV2.h to V4.h
/// @details CSignalHeapSim mimics how production uses the changing heap:
/// - the heap holds boost::weak_ptr<CSignal> so it does not keep signals
///   alive; signals leave by having their last owner released
/// - filters (IFilterCriteria) are added to the front of a CSortMethodList
///   on CSignalPtr so filtered signals sink, followed by the sort criteria
/// - CSignalSort wraps the list, locking both weak pointers on every
///   comparison and sorting expired signals to the top
/// - new signals wait in an incoming queue of 512, which is flushed into
///   the heap, followed by ClearEmptySignals, when full
/// - GetNextSignal flushes the queue, calls Top once, then pops expired
///   signals with TopNoSort and Pop until one locks, and returns it only
///   if it passes the filter
/// - frequency low to high steps up from the previous signal's frequency
///   and does not wrap around; signals below it tie
///
/// Configuration: filters active only, non excluded only, fits on tuner;
/// sorts best priority, frequency low to high, oldest.
///
/// Versions compared:
/// - original, v2, v3, v4 use the flow above unchanged
/// - "v4 + heap filter" takes the filters out of the sort list and gives
///   them to v4::CSortedChangingWeakHeap::SetFilter, so each signal is
///   checked once per step instead of in every comparison
///
/// Step test: a user steps one signal at a time. Between steps signal
/// frequencies, priorities and states change, and signals arrive and
/// leave. Each version runs the same simulated world and must return the
/// same signals. Only GetNextSignal is timed.
///
/// Drain test (CSortedHeap): fills the unchanging heap and pops every
/// item. v3::CSortedHeap and v4::CSortedHeap are v2::CSortedHeap.
///
/// Build and run (from this directory):
///   make
///   ./HeapBench [numSignals] [steps]

#include "../SortedHeap.h"
#include "../SortedHeapV2.h"
#include "../SortedHeapV3.h"
#include "../SortedHeapV4.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <stdint.h>

static long long comparisons=0; //!< number of signal comparisons
static long long filterChecks=0; //!< number of single filter evaluations

/// Physical status of a signal
struct SPhysicalStatus
{
	double mRssiDbm; //!< received signal strength
	double mSnrDb; //!< signal to noise ratio
	uint64_t mCenterFrequencyHz; //!< center frequency
};

/// Frequency range in Hz
struct SFrequencyRange
{
	uint64_t low; //!< lowest frequency
	uint64_t high; //!< highest frequency
};

///
/// Stand-in for an RF signal
class CSignal
{
public:
	CSignal(int newId, uint64_t frequencyHz, uint64_t bandwidthHz, uint64_t upTime)
		: id(newId), bandwidth(bandwidthHz), upTimeNs(upTime), priority(0), active(true), excluded(false)
	{
		status.mRssiDbm=-80.0;
		status.mSnrDb=10.0;
		status.mCenterFrequencyHz=frequencyHz;
	}

	const SPhysicalStatus& GetPhysicalStatus() const { return(status); }
	SPhysicalStatus& PhysicalStatus() { return(status); }
	SFrequencyRange GetFrequencyRange() const
	{
		SFrequencyRange range={status.mCenterFrequencyHz - bandwidth / 2, status.mCenterFrequencyHz + bandwidth / 2};
		return(range);
	}
	uint64_t GetUpTime() const { return(upTimeNs); }
	int GetMonitorPriority() const { return(priority); }
	bool IsActive() const { return(active); }
	bool IsExcluded() const { return(excluded); }

	int id; //!< unique id, used to check which signal was chosen
	uint64_t bandwidth; //!< bandwidth in Hz
	uint64_t upTimeNs; //!< up time in ns
	int priority; //!< monitor priority
	bool active; //!< still in the database
	bool excluded; //!< excluded by the user

private:
	SPhysicalStatus status; //!< physical status
};

/// typedef for smart pointer to a signal, held by the owners of signals
typedef boost::shared_ptr<CSignal> CSignalPtr;

/// typedef for weak pointer to a signal, the type the heaps hold
typedef boost::weak_ptr<CSignal> CSignalWeakPtr;

/// typedef for sort method on signals
typedef ISortMethod<CSignalPtr> SignalSortingMethod;

/// typedef for smart pointer to a sort method on signals
typedef boost::shared_ptr<SignalSortingMethod> ISignalSortPtr;

// The heaps return the greatest item, so LessThan(lhs, rhs) returns true
// when rhs should be chosen before lhs.

///
/// List of sort methods on signals that counts comparisons
class CSignalSortList : public CSortMethodList<CSignalPtr>
{
public:
	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		++comparisons;
		return(CSortMethodList<CSignalPtr>::LessThan(lhs, rhs));
	}
};

///
/// Base class for signal sorting, as in production
/// @details locks the weak pointers and calls the smart pointer sort.
/// An expired signal is greater than any live one so it comes to the top
/// to be thrown out.
class CSignalSort : public ISortMethod<CSignalWeakPtr>
{
public:
	virtual ~CSignalSort() {};
	virtual bool LessThan(const CSignalWeakPtr& lhs, const CSignalWeakPtr& rhs) const
	{
		// Lock the weak pointers
		CSignalPtr smartRhs = rhs.lock();
		CSignalPtr smartLhs = lhs.lock();

		// If they are equal
		if (!smartLhs && !smartRhs)
		{
			return(false);
		}
		// if the lhs is null
		else if (!smartLhs)
		{
			return(false);
		}
		// if the rhs is null
		else if (!smartRhs)
		{
			return(true);
		}
		else
		{
			// Call the smart pointer version as the pointers as valid
			return(mSmartPointerSort->LessThan(smartLhs, smartRhs));
		}
	};

	void SetSmartPointerSort(const ISignalSortPtr& sort)
	{
		mSmartPointerSort = sort;
	};

private:
	ISignalSortPtr mSmartPointerSort;
};

/// Sort by monitor priority, as in production
class CSortByPriority : public SignalSortingMethod
{
public:
	CSortByPriority(bool highest) : mHighest(highest) {}

	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		if (mHighest)
		{
			return(lhs->GetMonitorPriority() > rhs->GetMonitorPriority());
		}
		else
		{
			return(lhs->GetMonitorPriority() < rhs->GetMonitorPriority());
		}
	}

private:
	bool mHighest; //!< indicates to sort from highest to lowest
};

/// Sort by up time, as in production
class CSortByTime : public SignalSortingMethod
{
public:
	CSortByTime(bool newest, uint64_t rounding) : mNewest(newest), mRounding(rounding) {}

	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		uint64_t lhsValue = lhs->GetUpTime();
		uint64_t rhsValue = rhs->GetUpTime();
		// prevent divide by zero
		if(mRounding)
		{
			lhsValue = (lhsValue / mRounding) * mRounding;
			rhsValue = (rhsValue / mRounding) * mRounding;
		}
		if (mNewest)
		{
			return(lhsValue < rhsValue);
		}
		else
		{
			return(lhsValue > rhsValue);
		}
	}

private:
	bool mNewest; //!< indicates to sort from newest to oldest
	uint64_t mRounding; //!< what to round to
};

/// Sort by frequency from the previous signal, as in production
class CSortByFrequency : public SignalSortingMethod
{
public:
	CSortByFrequency(bool lowest, uint64_t* freq) : mLowest(lowest), mPreviousFrequency(freq) {}

	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		if (mLowest)
		{
			// if neither is valid
			if (lhs->GetPhysicalStatus().mCenterFrequencyHz < *mPreviousFrequency &&
				rhs->GetPhysicalStatus().mCenterFrequencyHz < *mPreviousFrequency)
			{
				return(false);
			}
			// if the lhs is not valid
			else if (lhs->GetPhysicalStatus().mCenterFrequencyHz < *mPreviousFrequency)
			{
				return(true);
			}
			// if the rhs is not valid
			else if (rhs->GetPhysicalStatus().mCenterFrequencyHz < *mPreviousFrequency)
			{
				return(false);
			}
			else
			{
				return(lhs->GetPhysicalStatus().mCenterFrequencyHz > rhs->GetPhysicalStatus().mCenterFrequencyHz);
			}
		}
		else
		{
			// if neither is valid
			if (lhs->GetPhysicalStatus().mCenterFrequencyHz > *mPreviousFrequency &&
				rhs->GetPhysicalStatus().mCenterFrequencyHz > *mPreviousFrequency)
			{
				return(false);
			}
			// if the lhs is not valid
			else if (lhs->GetPhysicalStatus().mCenterFrequencyHz > *mPreviousFrequency)
			{
				return(true);
			}
			// if the rhs is not valid
			else if (rhs->GetPhysicalStatus().mCenterFrequencyHz > *mPreviousFrequency)
			{
				return(false);
			}
			else
			{
				return(lhs->GetPhysicalStatus().mCenterFrequencyHz < rhs->GetPhysicalStatus().mCenterFrequencyHz);
			}
		}
	}

private:
	bool mLowest; //!< indicates to sort from lowest to highest
	uint64_t* mPreviousFrequency; //!< frequency of the previous signal
};

///
/// Interface class for filter criteria, as in production
class IFilterCriteria : public SignalSortingMethod
{
public:
	virtual ~IFilterCriteria() {};

	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		// Only return true if lhs is invalid and rhs is valid
		const bool validLhs = !IsFiltered(lhs);
		const bool validRhs = !IsFiltered(rhs);
		return (!validLhs && validRhs);
	}

	/// determines if a signal should be filtered out
	virtual bool IsFiltered(const CSignalPtr& signal) const = 0;
};

/// typedef for smart pointer to a filter
typedef boost::shared_ptr<IFilterCriteria> IFilterCriteriaPtr;

/// Filter list, as in production
class CFilterList : public IFilterCriteria
{
public:
	void Add(const IFilterCriteriaPtr& filter)
	{
		mFilters.push_back(filter);
	}

	virtual bool IsFiltered(const CSignalPtr& signal) const
	{
		// if any filter is true, then its true
		for(size_t i=0; i < mFilters.size(); i++)
		{
			if (mFilters[i]->IsFiltered(signal))
			{
				return(true);
			}
		}
		return(false);
	}

private:
	std::vector<IFilterCriteriaPtr> mFilters;
};

/// Filter for active signals only
class CFilterActiveOnly : public IFilterCriteria
{
public:
	virtual bool IsFiltered(const CSignalPtr& signal) const
	{
		++filterChecks;
		return(!signal->IsActive());
	}
};

/// Filter for excluded signals
class CFilterExcludedTargets : public IFilterCriteria
{
public:
	virtual bool IsFiltered(const CSignalPtr& signal) const
	{
		++filterChecks;
		return(signal->IsExcluded());
	}
};

/// typedef for tuner configurations, tuner id to frequency range
typedef std::map<int, SFrequencyRange> CDropTunerConfigurations;

/// Filter for signals that fit on a tuner, as in production
class CFilterFitsOnTuner : public IFilterCriteria
{
public:
	CFilterFitsOnTuner(const CDropTunerConfigurations* dtc) : mDtc(dtc) {}

	virtual bool IsFiltered(const CSignalPtr& signal) const
	{
		++filterChecks;
		SFrequencyRange range = signal->GetFrequencyRange();

		// filtered if it does not fit on a tuner
		for(CDropTunerConfigurations::const_iterator item=mDtc->begin(); item != mDtc->end(); ++item)
		{
			if (item->second.low <= range.low && range.high <= item->second.high)
			{
				return(false);
			}
		}
		return(true);
	}

private:
	const CDropTunerConfigurations* mDtc;
};

/// Sets the sorting of a heap of weak pointers to a CSignalSort that
/// wraps the smart pointer sort. Heap filters are not supported.
template< typename Heap >
void SetSignalSorting(Heap& heap, const ISignalSortPtr& sort, const IFilterCriteriaPtr& heapFilter)
{
	assert(!heapFilter);
	(void)heapFilter;
	boost::shared_ptr<CSignalSort> signalSort(new CSignalSort());
	signalSort->SetSmartPointerSort(sort);
	heap.SetSorting(signalSort);
}

/// v4::CSortedChangingWeakHeap takes the smart pointer sort directly,
/// and optionally the filter
void SetSignalSorting(v4::CSortedChangingWeakHeap<CSignal>& heap, const ISignalSortPtr& sort, const IFilterCriteriaPtr& heapFilter)
{
	heap.SetSorting(sort);
	if(heapFilter)
	{
		heap.SetFilter([heapFilter](const CSignalPtr& signal) { return(heapFilter->IsFiltered(signal)); });
	}
}

///
/// Simplified copy of production CSignalHeap
/// @param Heap changing heap type of weak pointers to signals
template< typename Heap >
class CSignalHeapSim
{
public:
	/// number of signals we hold incoming before processing them
	static const size_t MaxIncomingSignals = 512;

	/// Constructor
	/// @param [in] useHeapFilter give the filters to the heap instead of
	/// putting them in the sort list
	CSignalHeapSim(bool useHeapFilter)
		: mPreviousFrequency(0)
	{
		mIncomingQueue.reserve(MaxIncomingSignals);

		// tuners: eight 40 MHz ranges spread over 30 to 512 MHz
		for(int i=0; i < 8; i++)
		{
			SFrequencyRange range={30000000ull + i * 60000000ull, 70000000ull + i * 60000000ull};
			mDropTunerConfigs[i]=range;
		}

		boost::shared_ptr<CSignalSortList> sortList(new CSignalSortList());
		boost::shared_ptr<CFilterList> localFilter(new CFilterList());

		// Filters: active only, non excluded only, fits on tuner
		IFilterCriteriaPtr filters[]={IFilterCriteriaPtr(new CFilterActiveOnly()),
			IFilterCriteriaPtr(new CFilterExcludedTargets()),
			IFilterCriteriaPtr(new CFilterFitsOnTuner(&mDropTunerConfigs))};
		for(size_t i=0; i < sizeof(filters) / sizeof(filters[0]); i++)
		{
			localFilter->Add(filters[i]);
			if(!useHeapFilter)
			{
				sortList->AddMethod(filters[i]);
			}
		}
		mFilter=localFilter;

		// Sorts: best priority, frequency low to high, oldest
		sortList->AddMethod(ISignalSortPtr(new CSortByPriority(true)));
		sortList->AddMethod(ISignalSortPtr(new CSortByFrequency(true, &mPreviousFrequency)));
		sortList->AddMethod(ISignalSortPtr(new CSortByTime(false, 0)));

		SetSignalSorting(mHeap, sortList, useHeapFilter ? IFilterCriteriaPtr(localFilter) : IFilterCriteriaPtr());
	}

	/// Adds a signal, as in production
	bool AddSignal(const CSignalPtr& newSignal)
	{
		mIncomingQueue.push_back(CSignalWeakPtr(newSignal));
		if(mIncomingQueue.size() >= MaxIncomingSignals)
		{
			ProcessIncomingQueue();
			ClearEmptySignals();
		}
		return(!mFilter->IsFiltered(newSignal));
	}

	/// Gets the next signal, as in production
	CSignalPtr GetNextSignal()
	{
		ProcessIncomingQueue();

		CSignalPtr top;
		if (!mHeap.IsEmpty())
		{
			// Call the sort version first to get everything setup
			mHeap.Top();
			while (!mHeap.IsEmpty())
			{
				top = mHeap.TopNoSort().lock();
				if (top)
				{
					// Now make sure it passes the filter criteria
					if (mFilter && (mFilter->IsFiltered(top)))
					{
						// leave the item in the heap
						top.reset();
					}
					else
					{
						mHeap.Pop();
					}
					break;
				}
				mHeap.Pop();
			}
		}

		if(top)
		{
			mPreviousFrequency = top->GetPhysicalStatus().mCenterFrequencyHz;
		}
		return(top);
	}

private:
	void ProcessIncomingQueue()
	{
		if(!mIncomingQueue.empty())
		{
			mHeap.AddItems(mIncomingQueue);
			mIncomingQueue.clear();
		}
	}

	/// Removes expired signals, as in production
	void ClearEmptySignals()
	{
		typename Heap::HeapContainer& container = mHeap.GetContainer();
		typename Heap::HeapContainer::iterator itr = container.begin();
		while (itr != container.end())
		{
			if (!itr->lock())
			{
				itr = container.erase(itr);
			}
			else
			{
				++itr;
			}
		}
	}

	Heap mHeap; //!< heap under test
	std::vector<CSignalWeakPtr> mIncomingQueue; //!< signals waiting to be added
	boost::shared_ptr<CFilterList> mFilter; //!< filters
	uint64_t mPreviousFrequency; //!< frequency of the previous signal
	CDropTunerConfigurations mDropTunerConfigs; //!< tuner ranges
};

///
/// Simulated set of signals that a user steps through
/// @details Every version gets its own world built from the same seed, so
/// changes are identical as long as each version returns the same signals.
template< typename Heap >
class CWorld
{
public:
	/// Constructor
	/// @param [in] numSignals number of signals to start with
	/// @param [in] useHeapFilter see CSignalHeapSim
	CWorld(int numSignals, bool useHeapFilter)
		: generator(12345), nextId(0), now(0), heap(useHeapFilter)
	{
		for(int i=0; i < numSignals; i++)
		{
			Arrive();
		}
	}

	/// Method to change the signals between steps
	/// @details about 1% of signals drift in frequency, a few change
	/// priority, activity or exclusion, a few leave by releasing their
	/// owner, and a few arrive.
	void Change()
	{
		now+=500000000ull;

		std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
		std::uniform_int_distribution<int> drift(-200, 200);
		std::uniform_int_distribution<int> priority(0, 3);

		for(size_t i=0; i < live.size() / 100; i++)
		{
			live[pick(generator)]->PhysicalStatus().mCenterFrequencyHz+=drift(generator) * 12500;
		}
		for(int i=0; i < 3; i++)
		{
			live[pick(generator)]->priority=priority(generator);
		}
		for(int i=0; i < 2; i++)
		{
			CSignalPtr& signal=live[pick(generator)];
			signal->active=!signal->active;
		}
		live[pick(generator)]->excluded=true;

		// signals leaving
		for(int i=0; i < 5; i++)
		{
			size_t index=std::uniform_int_distribution<size_t>(0, live.size() - 1)(generator);
			live.erase(live.begin() + index);
		}

		// signals arriving
		for(int i=0; i < 6; i++)
		{
			Arrive();
		}
	}

	/// Method to step to the next signal
	/// @return id of the signal, -1 if none
	int Step()
	{
		CSignalPtr signal=heap.GetNextSignal();
		return(signal ? signal->id : -1);
	}

private:
	/// Creates a signal on a 12.5 kHz channel between 30 and 512 MHz
	void Arrive()
	{
		int channel=std::uniform_int_distribution<int>(0, 38560)(generator);
		uint64_t bandwidth=std::uniform_int_distribution<int>(1, 2)(generator) * 12500ull;
		now+=1000000ull;
		CSignalPtr signal(new CSignal(nextId++, 30000000ull + channel * 12500ull, bandwidth, now));
		signal->priority=(std::uniform_int_distribution<int>(0, 9)(generator) == 0) ? 1 : 0;
		signal->active=(std::uniform_int_distribution<int>(0, 9)(generator) != 0);
		live.push_back(signal);
		heap.AddSignal(signal);
	}

	std::mt19937 generator; //!< random numbers for changes
	int nextId; //!< id for the next new signal
	uint64_t now; //!< simulated time in ns
	std::vector<CSignalPtr> live; //!< owners of signals, in arrival order
	CSignalHeapSim<Heap> heap; //!< signal heap under test
};

/// Result of one benchmark run
struct SResult
{
	double ms; //!< total time in milliseconds
	double maxMs; //!< longest single step, in milliseconds
	long long comparisons; //!< signal comparisons
	long long filterChecks; //!< single filter evaluations
	std::vector<int> order; //!< ids in the order they were chosen
};

/// typedef for the clock used for timing
typedef std::chrono::steady_clock Clock;

/// Milliseconds since start
static double ElapsedMs(Clock::time_point start)
{
	return(std::chrono::duration<double, std::milli>(Clock::now()-start).count());
}

/// Steps through a simulated world, timing each GetNextSignal
template< typename Heap >
SResult RunSteps(int numSignals, int steps, bool useHeapFilter=false)
{
	CWorld<Heap> world(numSignals, useHeapFilter);
	SResult result={0.0, 0.0, 0, 0, std::vector<int>()};

	for(int step=0; step < steps; step++)
	{
		world.Change();

		long long comparisonsBefore=comparisons;
		long long filterChecksBefore=filterChecks;
		Clock::time_point start=Clock::now();
		int id=world.Step();
		double ms=ElapsedMs(start);

		result.comparisons+=comparisons - comparisonsBefore;
		result.filterChecks+=filterChecks - filterChecksBefore;
		result.ms+=ms;
		result.maxMs=std::max(result.maxMs, ms);
		result.order.push_back(id);
	}
	return(result);
}

/// Fills an unchanging heap, then pops every item with PopTop
template< typename Heap >
SResult RunDrain(int numSignals)
{
	// best priority, then oldest
	boost::shared_ptr<CSignalSortList> sorting(new CSignalSortList());
	sorting->AddMethod(ISignalSortPtr(new CSortByPriority(true)));
	sorting->AddMethod(ISignalSortPtr(new CSortByTime(false, 0)));

	std::mt19937 generator(1);
	std::vector<CSignalPtr> signals;
	for(int i=0; i < numSignals; i++)
	{
		signals.push_back(CSignalPtr(new CSignal(i, 0, 0, i)));
		signals.back()->priority=std::uniform_int_distribution<int>(0, 3)(generator);
	}
	std::shuffle(signals.begin(), signals.end(), generator);

	Heap heap;
	SetSignalSorting(heap, sorting, IFilterCriteriaPtr());
	heap.AddItems(std::vector<CSignalWeakPtr>(signals.begin(), signals.end()));

	SResult result={0.0, 0.0, 0, 0, std::vector<int>()};
	comparisons=0;
	Clock::time_point start=Clock::now();
	while(!heap.IsEmpty())
	{
		result.order.push_back(heap.PopTop().lock()->id);
	}
	result.ms=ElapsedMs(start);
	result.maxMs=result.ms;
	result.comparisons=comparisons;
	return(result);
}

/// Prints one row of a results table and checks it against the reference
/// @return true if the order matches the reference
bool Print(const char* name, const SResult& result, const SResult& reference, int count)
{
	bool same=(result.order == reference.order);
	std::printf("  %-28s %9.3f ms/step %9.3f ms max %11lld compares %10lld filter checks %8.1fx  %s\n",
		name, result.ms / count, result.maxMs, result.comparisons, result.filterChecks,
		reference.ms / (result.ms > 0 ? result.ms : 1e-9),
		same ? "same signals" : "DIFFERENT signals");
	return(same);
}

int main(int argc, char** argv)
{
	int numSignals=(argc > 1) ? std::atoi(argv[1]) : 5000;
	int steps=(argc > 2) ? std::atoi(argv[2]) : 100;
	bool allSame=true;

	std::printf("Step test: %d signals, %d steps (speedup is vs original)\n", numSignals, steps);
	SResult original=RunSteps< CSortedChangingHeap<CSignalWeakPtr> >(numSignals, steps);
	SResult stepV2=RunSteps< v2::CSortedChangingHeap<CSignalWeakPtr> >(numSignals, steps);
	SResult stepV3=RunSteps< v3::CSortedChangingHeap<CSignalWeakPtr> >(numSignals, steps);
	SResult stepV4=RunSteps< v4::CSortedChangingWeakHeap<CSignal> >(numSignals, steps);
	SResult stepV4Filter=RunSteps< v4::CSortedChangingWeakHeap<CSignal> >(numSignals, steps, true);
	allSame=Print("CSortedChangingHeap", original, original, steps) && allSame;
	allSame=Print("v2::CSortedChangingHeap", stepV2, original, steps) && allSame;
	allSame=Print("v3::CSortedChangingHeap", stepV3, original, steps) && allSame;
	allSame=Print("v4::CSortedChangingWeakHeap", stepV4, original, steps) && allSame;
	allSame=Print("v4 + heap filter", stepV4Filter, original, steps) && allSame;

	std::printf("\nDrain test: %d signals (times are for popping every signal)\n", numSignals);
	SResult drain=RunDrain< CSortedHeap<CSignalWeakPtr> >(numSignals);
	SResult drainV2=RunDrain< v2::CSortedHeap<CSignalWeakPtr> >(numSignals);
	allSame=Print("CSortedHeap", drain, drain, 1) && allSame;
	allSame=Print("v2::CSortedHeap (= v3, v4)", drainV2, drain, 1) && allSame;

	if(!allSame)
	{
		std::printf("FAIL: versions chose different signals\n");
		return(1);
	}
	return(0);
}
