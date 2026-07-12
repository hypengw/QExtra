export module qextra:task;
export import rstd;

export namespace qextra::prelude
{
template<typename T>
using task = rstd::async::coro<T>;
}
