"""The one exception bm_core raises for a file or a model that breaks the format."""


class ValidationError(ValueError):
    """A broken or refused file, section, model or rig.

    `message` says what is wrong, `where` says where (e.g. "MESH model 'house'
    face 12"); str() gives both. Every check in bm_core raises this, never a
    bare struct.error / IndexError, so a caller can catch one type.
    """

    def __init__(self, message, where=None):
        self.message = message
        self.where = where
        super().__init__(f"{where}: {message}" if where else message)


def need(cond, message, where=None):
    """raise ValidationError(message, where) unless cond"""
    if not cond:
        raise ValidationError(message, where)
