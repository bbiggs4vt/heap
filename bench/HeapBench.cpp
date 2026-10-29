///
/// Test bench comparing SortedHeap.h, SortedHeapV2.h and SortedHeapV3.h
/// @details The heaps hold boost::shared_ptr<CSignal>, as in production,
/// so every element copy the heap makes is an atomic reference count
/// update. Two tests are run:
///
/// Step test (CSortedChangingHeap): a user steps one signal at a time
/// using tiered criteria: user priority, then next frequency up from the
/// last signal, then oldest detection. Between steps signal frequencies
/// and priorities change and signals leave and arrive. Each version runs
/// the same simulated world and must step to the same signals. Only the
/// PopTop call is timed.
///
/// Drain test (CSortedHeap): fills the unchanging heap and pops every
/// item. v3::CSortedHeap is the same class as v2::CSortedHeap.
///
/// Build and run (from this directory):
///   make
///   ./HeapBench [numSignals] [steps]

#include "../SortedHeap.h"
#include "../SortedHeapV2.h"
#include "../SortedHeapV3.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

///
/// Stand-in for an RF signal structure
struct CSignal
{
	int id; //!< unique id, used to check which signal was chosen
	double frequency; //!< center frequency in Hz
	int priority; //!< priority attached by the user, higher is more important
	double detectTime; //!< time of detection in seconds

	CSignal(int newId, double newFrequency, int newPriority, double newDetectTime)
		: id(newId), frequency(newFrequency), priority(newPriority), detectTime(newDetectTime) {}
};

/// typedef for smart pointer to a signal, the type the heaps hold
typedef boost::shared_ptr<CSignal> CSignalPtr;

/// typedef for smart pointer to a sort method on signals
typedef boost::shared_ptr< ISortMethod<CSignalPtr> > ISignalSortPtr;

static long long comparisons=0; //!< number of LessThan calls

// The heaps return the greatest item, so LessThan(lhs, rhs) returns true
// when rhs should be chosen before lhs.

/// Sort by user priority
class CSortByPriority : public ISortMethod<CSignalPtr>
{
public:
	/// @param [in] newHighestFirst true to choose the highest priority first
	CSortByPriority(bool newHighestFirst) : highestFirst(newHighestFirst) {}

	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		++comparisons;
		return(highestFirst ? (lhs->priority < rhs->priority) : (rhs->priority < lhs->priority));
	}

private:
	bool highestFirst; //!< sort direction
};

/// Sort by time of detection
class CSortByDetectTime : public ISortMethod<CSignalPtr>
{
public:
	/// @param [in] newOldestFirst true to choose the oldest detection first
	CSortByDetectTime(bool newOldestFirst) : oldestFirst(newOldestFirst) {}

	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		++comparisons;
		return(oldestFirst ? (rhs->detectTime < lhs->detectTime) : (lhs->detectTime < rhs->detectTime));
	}

private:
	bool oldestFirst; //!< sort direction
};

/// Sort by the next frequency in line from the last signal chosen
/// @details signals at or behind the last frequency come after all
/// signals ahead of it, so the sweep does not step back to them.
class CSortByNextFrequency : public ISortMethod<CSignalPtr>
{
public:
	/// @param [in] newUpward true to sweep up in frequency, false for down
	CSortByNextFrequency(bool newUpward) : upward(newUpward), lastFrequency(0.0) {}

	/// Method to set the frequency of the last signal chosen
	/// @param [in] frequency frequency in Hz
	void SetLastFrequency(double frequency)
	{
		lastFrequency=frequency;
	}

	virtual bool LessThan(const CSignalPtr& lhs, const CSignalPtr& rhs) const
	{
		++comparisons;
		// a larger distance ahead of the last frequency is chosen later
		return(Distance(lhs->frequency) > Distance(rhs->frequency));
	}

private:
	/// Distance ahead of the last frequency in the sweep direction,
	/// wrapping around for signals at or behind it
	double Distance(double frequency) const
	{
		static const double wrap=1e12;
		double ahead=upward ? (frequency - lastFrequency) : (lastFrequency - frequency);
		return((ahead > 0.0) ? ahead : (ahead + wrap));
	}

	bool upward; //!< sweep direction
	double lastFrequency; //!< frequency of the last signal chosen
};

///
/// Simulated set of signals that a user steps through
/// @details Every version gets its own world built from the same seed, so
/// changes are identical as long as each version chooses the same signals.
/// Changes are picked from a list ordered by arrival, never from the heap
/// container, whose order differs between versions.
template< typename Heap >
class CWorld
{
public:
	/// Constructor
	/// @param [in] numSignals number of signals to start with
	CWorld(int numSignals)
		: generator(12345), nextId(0), now(0.0),
		nextFrequency(new CSortByNextFrequency(true)),
		sorting(new CSortMethodList<CSignalPtr>())
	{
		// user priority, then next frequency up, then oldest detection
		sorting->AddMethod(ISignalSortPtr(new CSortByPriority(true)));
		sorting->AddMethod(nextFrequency);
		sorting->AddMethod(ISignalSortPtr(new CSortByDetectTime(true)));
		heap.SetSorting(sorting);

		std::vector<CSignalPtr> signals;
		for(int i=0; i < numSignals; i++)
		{
			signals.push_back(NewSignal());
		}
		live=signals;
		heap.AddItems(signals);
		nextFrequency->SetLastFrequency(30e6);
	}

	/// Method to change the signals between steps
	/// @details about 1% of signals drift in frequency, a few change
	/// priority, and a few leave while others arrive.
	void Change()
	{
		now+=0.5;

		std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
		std::normal_distribution<double> drift(0.0, 2500.0);
		std::uniform_int_distribution<int> priority(0, 3);

		for(size_t i=0; i < live.size() / 100; i++)
		{
			live[pick(generator)]->frequency+=drift(generator);
		}
		for(int i=0; i < 3; i++)
		{
			live[pick(generator)]->priority=priority(generator);
		}

		// signals leaving
		for(int i=0; i < 5; i++)
		{
			size_t index=std::uniform_int_distribution<size_t>(0, live.size() - 1)(generator);
			Remove(live[index]);
		}

		// signals arriving, one more than left to replace the stepped signal
		for(int i=0; i < 6; i++)
		{
			CSignalPtr signal=NewSignal();
			live.push_back(signal);
			heap.AddItem(signal);
		}
	}

	/// Method to step to the next signal
	/// @return the chosen signal
	CSignalPtr Step()
	{
		CSignalPtr signal=heap.PopTop();
		live.erase(std::find(live.begin(), live.end(), signal));
		nextFrequency->SetLastFrequency(signal->frequency);
		return(signal);
	}

private:
	/// Creates a signal on a 12.5 kHz channel between 30 and 512 MHz
	CSignalPtr NewSignal()
	{
		int channel=std::uniform_int_distribution<int>(0, 38560)(generator);
		int priority=(std::uniform_int_distribution<int>(0, 9)(generator) == 0) ? 1 : 0;
		now+=0.001;
		return(CSignalPtr(new CSignal(nextId++, 30e6 + channel * 12500.0, priority, now)));
	}

	/// Removes a signal from the arrival list and the heap
	/// @param [in] signal signal to remove, a copy since it may refer into live
	void Remove(CSignalPtr signal)
	{
		live.erase(std::find(live.begin(), live.end(), signal));
		typename Heap::HeapContainer& container=heap.GetContainer();
		container.erase(std::find(container.begin(), container.end(), signal));
	}

	std::mt19937 generator; //!< random numbers for changes
	int nextId; //!< id for the next new signal
	double now; //!< simulated time in seconds
	boost::shared_ptr<CSortByNextFrequency> nextFrequency; //!< frequency sweep criteria
	boost::shared_ptr< CSortMethodList<CSignalPtr> > sorting; //!< tiered criteria
	std::vector<CSignalPtr> live; //!< signals in the list, in arrival order
	Heap heap; //!< heap under test
};

/// Result of one benchmark run
struct SResult
{
	double ms; //!< total time in milliseconds
	double maxMs; //!< longest single PopTop, in milliseconds
	long long comparisons; //!< LessThan calls
	std::vector<int> order; //!< ids in the order they were chosen
};

/// typedef for the clock used for timing
typedef std::chrono::steady_clock Clock;

/// Milliseconds since start
static double ElapsedMs(Clock::time_point start)
{
	return(std::chrono::duration<double, std::milli>(Clock::now()-start).count());
}

/// Steps through a simulated world, timing each step
template< typename Heap >
SResult RunSteps(int numSignals, int steps)
{
	CWorld<Heap> world(numSignals);
	SResult result={0.0, 0.0, 0, std::vector<int>()};

	for(int step=0; step < steps; step++)
	{
		world.Change();

		long long before=comparisons;
		Clock::time_point start=Clock::now();
		CSignalPtr signal=world.Step();
		double ms=ElapsedMs(start);

		result.comparisons+=comparisons - before;
		result.ms+=ms;
		result.maxMs=std::max(result.maxMs, ms);
		result.order.push_back(signal->id);
	}
	return(result);
}

/// Fills an unchanging heap, then pops every item with PopTop
template< typename Heap >
SResult RunDrain(int numSignals)
{
	// user priority, then oldest detection
	boost::shared_ptr< CSortMethodList<CSignalPtr> > sorting(new CSortMethodList<CSignalPtr>());
	sorting->AddMethod(ISignalSortPtr(new CSortByPriority(true)));
	sorting->AddMethod(ISignalSortPtr(new CSortByDetectTime(true)));

	std::mt19937 generator(1);
	std::vector<CSignalPtr> signals;
	for(int i=0; i < numSignals; i++)
	{
		signals.push_back(CSignalPtr(new CSignal(i, 0.0, std::uniform_int_distribution<int>(0, 3)(generator), i * 0.001)));
	}
	std::shuffle(signals.begin(), signals.end(), generator);

	Heap heap(sorting);
	heap.AddItems(signals);

	SResult result={0.0, 0.0, 0, std::vector<int>()};
	comparisons=0;
	Clock::time_point start=Clock::now();
	while(!heap.IsEmpty())
	{
		result.order.push_back(heap.PopTop()->id);
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
	std::printf("  %-26s %10.3f ms/step %10.3f ms max %14lld compares  %8.1fx  %s\n",
		name, result.ms / count, result.maxMs, result.comparisons,
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
	SResult original=RunSteps< CSortedChangingHeap<CSignalPtr> >(numSignals, steps);
	SResult stepV2=RunSteps< v2::CSortedChangingHeap<CSignalPtr> >(numSignals, steps);
	SResult stepV3=RunSteps< v3::CSortedChangingHeap<CSignalPtr> >(numSignals, steps);
	allSame=Print("CSortedChangingHeap", original, original, steps) && allSame;
	allSame=Print("v2::CSortedChangingHeap", stepV2, original, steps) && allSame;
	allSame=Print("v3::CSortedChangingHeap", stepV3, original, steps) && allSame;

	std::printf("\nDrain test: %d signals (times are for popping every signal)\n", numSignals);
	SResult drain=RunDrain< CSortedHeap<CSignalPtr> >(numSignals);
	SResult drainV2=RunDrain< v2::CSortedHeap<CSignalPtr> >(numSignals);
	allSame=Print("CSortedHeap", drain, drain, 1) && allSame;
	allSame=Print("v2::CSortedHeap (= v3)", drainV2, drain, 1) && allSame;

	if(!allSame)
	{
		std::printf("FAIL: versions chose different signals\n");
		return(1);
	}
	return(0);
}
