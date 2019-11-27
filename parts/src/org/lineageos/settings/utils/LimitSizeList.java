package org.lineageos.settings.utils;

import java.util.LinkedList;

public class LimitSizeList<E> extends LinkedList<E> {
    private int limit;

    public LimitSizeList(int limit2) {
        this.limit = limit2;
    }

    @Override
    public boolean add(E e) {
        if (size() + 1 > this.limit) {
            super.removeFirst();
        }
        return super.add(e);
    }

    public int getLimit() {
        return this.limit;
    }

    public boolean isFull() {
        return size() == this.limit;
    }
}
