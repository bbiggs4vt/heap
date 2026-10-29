#pragma once

#include <functional>
#include "SortedHeap.h"

///
/// Version 2 of CSortedHeap and CSortedChangingHeap.
/// @details Identical to the classes in SortedHeap.h except that the
/// heap algorithms receive the comparator as std::cref(*this) instead
/// of *this. The std heap functions take their comparator by value, so
/// passing *this copied the entire container on every make_heap,
/// push_heap and pop_heap call. ISortMethod and CSortMethodList are
/// shared with SortedHeap.h, so sort methods work with either version.
namespace v2
{

///
/// Templated class to implement a heap of unchanging items
/// @details Heaps are structures where the entire set is not
/// sorted, but the first item is sorted properly. The items
/// in this heap are not expected to change over time, if you
/// expect changing value, please use CSortedChangingHeap.
template< typename T >
class CSortedHeap
{
public:
	/// typedef for smart pointer
	typedef boost::shared_ptr< ISortMethod<T> > ISortMethodPtr;

	/// typedef of container
	typedef std::deque<T> HeapContainer;

	/// Constructor
	/// @details constructs with no sorting, note that if
	/// items are added or popped with no sorting it is an
	/// error condition. SetSorting must be called before
	/// any other methods are called.
	CSortedHeap()
	{
		
	}

	/// Constructor
	/// @param [in] newSorting sorting method
	CSortedHeap(const ISortMethodPtr& newSorting)
	{
		SetSorting(newSorting);
	}

	/// operator to allow this class to be a comparitor
	/// @details defers to LessThan function of sorting method
	/// @param [in] lhs left hand side of operation
	/// @param [in] rhs right hand side of operation
	/// @return same as LessThan function
	bool operator()(const T& lhs, const T& rhs) const
	{
		assert(sorting);
		
		return(sorting->LessThan(lhs, rhs));		
	};

	/// Method to set the sorting for the heap
	/// @param [in] newSorting sorting method to use
	void SetSorting(const ISortMethodPtr& newSorting)
	{
		sorting=newSorting;

		// re-make the heap, since the sort algorithm changed
		std::make_heap(container.begin(), container.end(), std::cref(*this));
	}

	/// Method to add an item to the heap
	/// @param [in] item item to add
	void AddItem(const T& item)
	{
		container.push_back(item);
		std::push_heap(container.begin(), container.end(), std::cref(*this));
	}

	/// Method to add an array of items to the heap
	/// @param [in] items items to add
	void AddItems(const std::vector<T>& items)
	{
		// Move items to the container
		std::move(items.begin(), items.end(), std::back_inserter(container));

		// Assuming a large add, we should call
		// the main function to make it
		std::make_heap(container.begin(), container.end(), std::cref(*this));
	}

	/// Method to get the item at the top of the heap
	/// @details this method will not remove the item from the heap
	/// @return top item
	T Top()
	{
		return(container.front());
	}

	/// Method to get the item at the top of the heap
	/// @details this method will remove the item from the heap
	/// @return top item
	T PopTop()
	{
		// get the value
		T value = Top();

		// Pop off the heap
		std::pop_heap(container.begin(), container.end(), std::cref(*this));
		container.pop_back();

		return(value);
	}

	/// Method that returns whether or not the heap is empty
	/// @return true for empty, false otherwise
	bool IsEmpty()
	{
		return(container.empty());
	}

	/// Method that returns the size of the underlying container
	/// size_t size of container
	size_t Size()
	{
		return(container.size());
	}

	/// Method used to get to the underlying container for the heap
	/// reference to underlying container
	HeapContainer& GetContainer()
	{
		return(container);
	}

private:
	HeapContainer container; //<! container for heap
	ISortMethodPtr sorting; //!< sorting method
};

///
/// Templated class to implement a heap of changing items
/// @details Heaps are structures where the entire set is not
/// sorted, but the first item is sorted properly.
/// The items in this heap can be changing value
/// thus the sort is only done periodically and when an
/// item is popped off.
template< typename T >
class CSortedChangingHeap
{
public:
	/// typedef for smart pointer
	typedef boost::shared_ptr< ISortMethod<T> > ISortMethodPtr;

	/// typedef of container
	typedef std::vector<T> HeapContainer;

	/// Constructor
	/// @details constructs with no sorting, note that if
	/// items are added or popped with no sorting it is an
	/// error condition. SetSorting must be called before
	/// any other methods are called.
	CSortedChangingHeap()
	{
		
	}

	/// Constructor
	/// @details constructs with no sorting, note that if
	/// items are added or popped with no sorting it is an
	/// error condition. SetSorting must be called before
	/// any other methods are called.
	CSortedChangingHeap(size_t start)
	{
		container.reserve(start);
	}

	/// Constructor
	CSortedChangingHeap(const ISortMethodPtr& newSorting)
	{
		SetSorting(newSorting);
	}

	/// operator to allow this class to be a comparitor
	/// @details defers to LessThan function of sorting method
	/// @param [in] lhs left hand side of operation
	/// @param [in] rhs right hand side of operation
	/// @return same as LessThan function
	bool operator()(const T& lhs, const T& rhs) const
	{
		assert(sorting);

		return(sorting->LessThan(lhs, rhs));
	};

	/// Method to set the sorting for the heap
	/// @param [in] newSorting sorting method to use
	void SetSorting(const ISortMethodPtr& newSorting)
	{
		sorting=newSorting;

		// re-make the heap, since the sort algorithm changed
		std::make_heap(container.begin(), container.end(), std::cref(*this));
	}

	void Clear()
	{
		container.clear();
	}

	/// Method to add an item to the heap
	/// @details items are not sorted after calling this
	/// method. Call Top if you want to sort them manually.
	/// @param [in] item item to add
	void AddItem(const T& item)
	{
		container.push_back(item);
	}

	/// Method to add an array of items to the heap
	/// @details items added are resorted after add. This method
	/// should not be called with a small number of items if
	/// possible.
	/// @param [in] items items to add
	void AddItems(const std::vector<T>& items)
	{
		assert(sorting);

		// Move items to the container
		std::move(items.begin(), items.end(), std::back_inserter(container));
	}

	/// Method to get the item at the top of the heap
	/// @details this method will not remove the item from the heap
	/// @return top item
	T Top()
	{
		// We have to make the heap before calling front
		std::make_heap(container.begin(), container.end(), std::cref(*this));

		return(container.front());
	}

	/// Method to get the item at the top of the heap
	/// @details this method will remove the item from the heap
	/// @return top item
	T PopTop()
	{
		// get the value
		T value = Top();

		// Pop off the heap
		Pop();

		return(value);
	}

	/// Method to get the item at the top of the heap (will not sort the heap!)
	/// @details this method will not remove the item from the heap. It assumes
	/// the heap has already been sorted by a call to the normal Top or PopTop
	/// method.
	/// @return top item
	T TopNoSort()
	{
		return(container.front());
	}

	/// Method to get the item at the top of the heap (will not sort the heap!)
	/// @details this method will remove the item from the heap. It assumes
	/// the heap has already been sorted by a call to the normal Top or PopTop
	/// method.
	/// @return top item
	T PopTopNoSort()
	{
		// get the value
		T value = TopNoSort();

		// Pop off the heap
		Pop();

		return(value);
	}

	/// Method used to pop the heap
	/// @details this method will remove the item from the heap
	void Pop()
	{	
		// Pop off the heap
		std::pop_heap(container.begin(), container.end(), std::cref(*this));
		container.pop_back();
	}

	/// Method that returns whether or not the heap is empty
	/// @return true for empty, false otherwise
	bool IsEmpty()
	{
		return(container.empty());
	}

	/// Method that returns the size of the underlying container
	/// size_t size of container
	size_t Size()
	{
		return(container.size());
	}

	/// Method used to get to the underlying container for the heap
	/// reference to underlying container
	HeapContainer& GetContainer()
	{
		return(container);
	}

private:
	HeapContainer container; //<! container for heap
	ISortMethodPtr sorting; //!< sorting method
};

} // namespace v2
