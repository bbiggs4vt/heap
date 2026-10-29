///
/// Test bench comparing SortedHeap.h against SortedHeapV2.h
/// @details Fills each heap with the same random signals, pops every
/// item, and reports time, element copies and comparator calls. The
/// pop order of the two versions is checked to be identical.
///
/// Build and run (from this directory):
///   make
///   ./HeapBench [numSignals] [repetitions]

#include "../SortedHeap.h"
#include "../SortedHeapV2.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

///
/// Stand-in for an RF signal that counts how often it is copied
struct CSignal
{
	int id; //!< unique id, used to check pop order
	double rssi; //!< received signal strength
	double snr; //!< signal to noise ratio

	static long long copies; //!< number of copies made

	CSignal(int newId, double newRssi, double newSnr)
		: id(newId), rssi(newRssi), snr(newSnr) {}

	CSignal(const CSignal& other)
		: id(other.id), rssi(other.rssi), snr(other.snr) { ++copies; }

	CSignal& operator=(const CSignal& other)
	{
		id=other.id; rssi=other.rssi; snr=other.snr;
		++copies;
		return(*this);
	}
};
long long CSignal::copies=0;

static long long comparisons=0; //!< number of LessThan calls

/// Sort by SNR
class CSortBySnr : public ISortMethod<CSignal>
{
public:
	virtual bool LessThan(const CSignal& lhs, const CSignal& rhs) const
	{
		++comparisons;
		return(lhs.snr < rhs.snr);
	}
};

/// Sort by RSSI
class CSortByRssi : public ISortMethod<CSignal>
{
public:
	virtual bool LessThan(const CSignal& lhs, const CSignal& rhs) const
	{
		++comparisons;
		return(lhs.rssi < rhs.rssi);
	}
};

/// Result of one benchmark run
struct SResult
{
	double ms; //!< time to pop everything, in milliseconds
	long long copies; //!< element copies while popping
	long long comparisons; //!< LessThan calls while popping
	std::vector<int> order; //!< ids in the order they were popped
};

/// Fills a heap, then pops every item with PopTop
template< typename Heap >
SResult Run(Heap& heap, const std::vector<CSignal>& signals)
{
	SResult result;
	result.order.reserve(signals.size());

	heap.AddItems(signals);

	CSignal::copies=0;
	comparisons=0;
	std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();

	while(!heap.IsEmpty())
	{
		result.order.push_back(heap.PopTop().id);
	}

	result.ms=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
	result.copies=CSignal::copies;
	result.comparisons=comparisons;
	return(result);
}

/// Prints one row of the results table
void Print(const char* name, const SResult& result)
{
	std::printf("  %-26s %12.2f ms %16lld copies %14lld compares\n",
		name, result.ms, result.copies, result.comparisons);
}

/// Prints the speedup and checks both versions popped the same order
bool Compare(const SResult& v1, const SResult& v2)
{
	bool same=(v1.order == v2.order);
	std::printf("  %-26s %12.1fx faster, pop order %s\n\n",
		"v2 vs original:", v1.ms / (v2.ms > 0 ? v2.ms : 1e-9), same ? "identical" : "DIFFERENT");
	return(same);
}

int main(int argc, char** argv)
{
	int numSignals=(argc > 1) ? std::atoi(argv[1]) : 5000;
	int repetitions=(argc > 2) ? std::atoi(argv[2]) : 1;

	// SNR first, RSSI as a tie-breaker, like a user-selected criteria list
	boost::shared_ptr< CSortMethodList<CSignal> > sorting(new CSortMethodList<CSignal>());
	sorting->AddMethod(boost::shared_ptr< ISortMethod<CSignal> >(new CSortBySnr()));
	sorting->AddMethod(boost::shared_ptr< ISortMethod<CSignal> >(new CSortByRssi()));

	bool allSame=true;
	for(int rep=0; rep < repetitions; rep++)
	{
		// SNR is quantized to whole dB so the RSSI tie-breaker gets used
		std::mt19937 generator(rep + 1);
		std::uniform_real_distribution<double> rssi(-120.0, -20.0);
		std::uniform_int_distribution<int> snr(0, 30);

		std::vector<CSignal> signals;
		signals.reserve(numSignals);
		for(int i=0; i < numSignals; i++)
		{
			signals.push_back(CSignal(i, rssi(generator), snr(generator)));
		}

		std::printf("Run %d: %d signals\n", rep + 1, numSignals);

		CSortedChangingHeap<CSignal> changing(sorting);
		v2::CSortedChangingHeap<CSignal> changingV2(sorting);
		SResult changingResult=Run(changing, signals);
		SResult changingResultV2=Run(changingV2, signals);
		Print("CSortedChangingHeap", changingResult);
		Print("v2::CSortedChangingHeap", changingResultV2);
		allSame=Compare(changingResult, changingResultV2) && allSame;

		CSortedHeap<CSignal> fixed(sorting);
		v2::CSortedHeap<CSignal> fixedV2(sorting);
		SResult fixedResult=Run(fixed, signals);
		SResult fixedResultV2=Run(fixedV2, signals);
		Print("CSortedHeap", fixedResult);
		Print("v2::CSortedHeap", fixedResultV2);
		allSame=Compare(fixedResult, fixedResultV2) && allSame;
	}

	if(!allSame)
	{
		std::printf("FAIL: v2 popped signals in a different order\n");
		return(1);
	}
	return(0);
}
