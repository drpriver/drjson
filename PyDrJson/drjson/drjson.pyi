from typing import Tuple, Any, Protocol, Callable, overload, Final, Union, Optional
version: Tuple[int, int, int]
__version__: str
ERROR: int
NUMBER: int
INTEGER: int
UINTEGER: int
STRING: int
ARRAY: int
OBJECT: int
NULL: int
BOOL: int
ARRAY_VIEW: int
OBJECT_KEYS: int
OBJECT_VALUES: int
OBJECT_ITEMS: int
APPEND_NEWLINE: int
PRETTY_PRINT: int

class Writer(Protocol):
    def write(self, s:str) -> None:
        ...

class Reader(Protocol):
    def read(self) -> str | bytes:
        ...

def parse(text:str, braceless:bool=False) -> Value:
    ...

def loads(text:str, braceless:bool=False) -> Value:
    ...

def load(file:str|Reader, braceless:bool=False) -> Value:
    ...


class Ctx:
    def parse(self, text:str, braceless:bool=False) -> Value:
        ...

    def loads(self, text:str, braceless:bool=False) -> Value:
        ...

    def load(self, file:str|Reader, braceless:bool=False) -> Value:
        ...

    def make(self, value: Any) -> Value:
        ...

    def mem(self) -> tuple:
        ...


class Value:
    ctx: Final[Ctx]
    kind: Final[int]

    def __repr__(self) -> str:
        ...

    def __getitem__(self, k:Union[int, str]) -> Value:
        ...

    def __delitem__(self, k:Union[int, str]) -> None:
        ...

    def __setitem__(self, k:Union[int, str], v:Any) -> None:
        ...

    def __len__(self) -> int:
        ...

    def py(self) -> Union[int, str, float, list, dict]:
        ...

    def query(self, query:str, type:Optional[int]=None) -> Value:
        ...

    def clear(self) -> None:
        ...

    # only valid if kind == ARRAY
    def append(self, item:Any) -> None:
        ...

    # only valid if kind == ARRAY
    def pop(self) -> Value:
        ...

    # only valid if kind == ARRAY
    def insert(self, whence:int, item:Any) -> None:
        ...

    @overload
    def dump(self, writer:None=None, flags:int=0) -> str:
        ...

    @overload
    def dump(self, writer:Union[Writer, Callable[[str], None]], flags:int=0) -> None:
        ...

    @overload
    def dump(self, writer:Union[None, Writer, Callable[[str], None]]=None, flags:int=0) -> Union[str, None]:
        ...

    def keys(self) -> list[str]:
        ...

    def values(self) -> list[Value]:
        ...

    def items(self) -> list[tuple[str, Value]]:
        ...
